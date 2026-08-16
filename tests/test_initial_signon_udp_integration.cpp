#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/initial_signon_stage.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;
namespace auth = hlclient::auth;

inline constexpr std::string_view kSyntheticAuthenticationMarker =
    "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
inline constexpr std::string_view kSyntheticEarlyText =
    "FAKE-HLDS INITIAL SIGNON TEXT";

class CountingAuthenticationLifetime final
    : public auth::IAuthenticationSessionLifetime {
public:
    explicit CountingAuthenticationLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingAuthenticationLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto raw = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{raw.begin(), raw.end()};
}

[[nodiscard]] std::vector<std::byte> bytes(
    const std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(
        values,
        std::back_inserter(output),
        [](const std::uint8_t value) { return std::byte{value}; });
    return output;
}

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
}

void append_u16_le(std::vector<std::byte>& output, const std::uint16_t value)
{
    output.push_back(std::byte{static_cast<std::uint8_t>(value & 0xffU)});
    output.push_back(
        std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xffU)});
}

void append_u32_le(std::vector<std::byte>& output, const std::uint32_t value)
{
    output.push_back(std::byte{static_cast<std::uint8_t>(value & 0xffU)});
    output.push_back(
        std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xffU)});
    output.push_back(
        std::byte{static_cast<std::uint8_t>((value >> 16U) & 0xffU)});
    output.push_back(
        std::byte{static_cast<std::uint8_t>((value >> 24U) & 0xffU)});
}

[[nodiscard]] std::vector<std::byte> challenge_response(
    const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 " + std::to_string(challenge) +
              " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> accept_response(
    const network::NetworkAddress client)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "B 1 \"" + client.to_string() + "\" 0 10210";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] goldsrc::PreparedConnectRequest prepared_request()
{
    std::vector<std::byte> suffix(
        goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = bytes(kSyntheticAuthenticationMarker);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto material = goldsrc::AuthenticationMaterial::create(
        bytes(kSyntheticProtectedAuthentication),
        suffix);
    REQUIRE(material);
    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    auto prepared = goldsrc::prepare_connect_request(
        {},
        std::move(*material.value),
        profile);
    REQUIRE(prepared);
    return std::move(*prepared.value);
}

struct PreparedWithSession {
    goldsrc::PreparedConnectRequest request;
    auth::AuthenticationSession session;
};

[[nodiscard]] PreparedWithSession prepared_request_with_session(
    std::size_t& releases)
{
    std::vector<std::byte> suffix(
        goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = bytes(kSyntheticAuthenticationMarker);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto material = goldsrc::AuthenticationMaterial::create(
        bytes(kSyntheticProtectedAuthentication),
        suffix);
    REQUIRE(material);

    auth::AuthenticationSession session{
        std::move(*material.value),
        std::make_unique<CountingAuthenticationLifetime>(releases),
    };
    auto transferred = session.take_material();
    REQUIRE(transferred);
    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    auto prepared = goldsrc::prepare_connect_request(
        {},
        std::move(*transferred),
        profile);
    REQUIRE(prepared);
    return PreparedWithSession{std::move(*prepared.value), std::move(session)};
}

[[nodiscard]] network::Datagram receive_bounded(
    network::UdpSocket& socket,
    const std::size_t maximum_size)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto received = socket.receive(maximum_size);
        if (received.status == network::ReceiveStatus::received) {
            REQUIRE(received.datagram);
            return std::move(*received.datagram);
        }
        if (received.status == network::ReceiveStatus::error ||
            received.status == network::ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::yield();
    }
    throw std::runtime_error{"Timed out waiting for bounded fake-HLDS sign-on traffic"};
}

void require_no_datagram(network::UdpSocket& socket)
{
    const auto received = socket.receive(goldsrc::kMaximumNetchanDatagramSize);
    if (received.status == network::ReceiveStatus::received) {
        FAIL("Unexpected post-boundary fake-HLDS datagram");
    }
    if (received.status == network::ReceiveStatus::error ||
        received.status == network::ReceiveStatus::truncated) {
        FAIL(received.error);
    }
    CHECK(received.status == network::ReceiveStatus::would_block);
}

void send_server_datagram(
    network::UdpSocket& server,
    const network::NetworkAddress client,
    const std::span<const std::byte> datagram,
    std::string& error)
{
    REQUIRE(server.send_to(client, datagram, error));
}

[[nodiscard]] std::vector<std::byte> semantic_service_payload(
    const std::size_t boundary_body_size)
{
    REQUIRE(boundary_body_size > 0U);
    std::vector<std::byte> payload;
    payload.push_back(std::byte{0x08U});
    const auto text = bytes(kSyntheticEarlyText);
    payload.insert(payload.end(), text.begin(), text.end());
    payload.push_back(std::byte{0U});
    payload.push_back(std::byte{0x0bU});

    std::uint32_t state = 0x6d2b'79f5U;
    for (std::size_t index = 0U; index < boundary_body_size; ++index) {
        state = state * 1'664'525U + 1'013'904'223U;
        payload.push_back(std::byte{static_cast<std::uint8_t>(state >> 24U)});
    }
    return payload;
}

// Independent standard-BZip2 fixture construction. Production envelope and
// service decoders are not used to build expected fake-server bytes.
[[nodiscard]] std::vector<std::byte> service_envelope(
    const std::span<const std::byte> semantic_payload)
{
    REQUIRE_FALSE(semantic_payload.empty());
    REQUIRE(semantic_payload.size() <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> source;
    source.reserve(semantic_payload.size());
    std::ranges::transform(
        semantic_payload,
        std::back_inserter(source),
        [](const std::byte value) {
            return static_cast<char>(std::to_integer<std::uint8_t>(value));
        });
    const auto bound = source.size() + source.size() / 100U + 601U;
    REQUIRE(bound <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> compressed(bound);
    auto compressed_size = static_cast<unsigned int>(compressed.size());
    REQUIRE(BZ2_bzBuffToBuffCompress(
                compressed.data(),
                &compressed_size,
                source.data(),
                static_cast<unsigned int>(source.size()),
                9,
                0,
                30) == BZ_OK);
    compressed.resize(compressed_size);

    auto envelope = bytes({0x42U, 0x5aU, 0x32U, 0x00U});
    envelope.reserve(envelope.size() + compressed.size());
    std::ranges::transform(
        compressed,
        std::back_inserter(envelope),
        [](const char value) {
            return std::byte{static_cast<std::uint8_t>(
                static_cast<unsigned char>(value))};
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> encode_server_packet(
    const std::uint32_t packet_sequence,
    const bool reliable,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    std::vector<std::byte> payload)
{
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{reliable, false},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                reliable_acknowledgement,
            },
        },
        {},
        std::move(payload),
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

// Independent two-slot descriptor fixture for fake-HLDS incoming traffic.
[[nodiscard]] std::vector<std::byte> independent_server_fragment(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    const std::span<const std::byte> fragment)
{
    REQUIRE_FALSE(fragment.empty());
    REQUIRE(fragment.size() <= goldsrc::kStockProtocol48NormalFragmentChunkSize);
    std::vector<std::byte> body;
    body.push_back(std::byte{1U});
    append_u32_le(
        body,
        (static_cast<std::uint32_t>(fragment_index) << 16U) |
            static_cast<std::uint32_t>(fragment_count));
    append_u16_le(body, 0U);
    append_u16_le(body, static_cast<std::uint16_t>(fragment.size()));
    body.push_back(std::byte{0U});
    body.insert(body.end(), fragment.begin(), fragment.end());
    goldsrc::encode_netchan_payload(body, sequence(packet_sequence));

    std::vector<std::byte> datagram;
    append_u32_le(
        datagram,
        packet_sequence | goldsrc::kNetchanReliableSequenceFlag |
            goldsrc::kNetchanFragmentSequenceFlag);
    append_u32_le(
        datagram,
        acknowledgement |
            (reliable_acknowledgement ? goldsrc::kNetchanReliableSequenceFlag : 0U));
    datagram.insert(datagram.end(), body.begin(), body.end());
    return datagram;
}

[[nodiscard]] goldsrc::InitialSignonConfig integration_config()
{
    goldsrc::InitialSignonConfig config;
    config.driver.channel_inactivity_timeout = 1s;
    config.driver.fragment_transfer_timeout = 250ms;
    config.driver.maximum_datagram_size = 1'100U;
    config.driver.maximum_fragment_datagram_size = 1'100U;
    config.driver.maximum_unreliable_payload_size =
        config.driver.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    config.driver.maximum_datagrams_per_update = 8U;
    config.driver.maximum_outgoing_packets_per_update = 8U;
    config.driver.maximum_events = 32U;
    config.maximum_events = 32U;
    config.maximum_driver_events_per_update = 32U;
    return config;
}

struct AcceptedPair {
    network::NetworkAddress server_endpoint;
    network::NetworkAddress client_endpoint;
    goldsrc::InitialSignonTimePoint accepted_at{};
};

[[nodiscard]] AcceptedPair perform_handshake(
    network::UdpSocket& server,
    network::UdpDatagramTransport& transport,
    const network::NetworkAddress server_endpoint,
    const std::size_t run,
    std::string& error)
{
    goldsrc::ChallengeExchangeConfig challenge_config;
    challenge_config.retry_interval = 100ms;
    challenge_config.timeout = 1s;
    challenge_config.maximum_attempts = 2U;
    challenge_config.maximum_datagrams_per_update = 4U;
    goldsrc::ConnectResponseWaitConfig response_config;
    response_config.timeout = 1s;
    response_config.maximum_datagrams_per_update = 4U;
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        server_endpoint,
        goldsrc::HandshakeStopPoint::connect_response,
        prepared_request(),
        challenge_config,
        {},
        {},
        response_config};

    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{static_cast<std::int64_t>(run + 1U)};
    REQUIRE(handshake.start(epoch));
    auto request = receive_bounded(
        server,
        goldsrc::kMaximumConnectionlessChallengeDatagramSize);
    const auto expected_challenge = goldsrc::build_getchallenge_request();
    REQUIRE(expected_challenge);
    CHECK(request.payload == *expected_challenge.datagram);
    const auto client_endpoint = request.source;

    constexpr std::uint32_t challenge = 0x7f00'2410U;
    send_server_datagram(server, client_endpoint, challenge_response(challenge), error);
    handshake.update(epoch + 1ms);
    auto connect = receive_bounded(server, goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect.source == client_endpoint);
    const auto parsed = goldsrc::parse_connect_request(connect.payload, [] {
        auto profile = goldsrc::ConnectCompatibilityProfile{};
        profile.protected_authentication_is_ascii_hex = false;
        return profile;
    }());
    REQUIRE(parsed);
    CHECK(parsed.request->challenge() == challenge);

    send_server_datagram(
        server,
        client_endpoint,
        accept_response(client_endpoint),
        error);
    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() == goldsrc::GoldSrcHandshakeState::accepted);
    REQUIRE(handshake.terminal());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == client_endpoint);
    REQUIRE(handshake.connect_response());
    CHECK(std::holds_alternative<goldsrc::ConnectAccepted>(
        *handshake.connect_response()));
    CHECK(handshake.connect_send_attempts() == 1U);
    return AcceptedPair{server_endpoint, client_endpoint, epoch + 2ms};
}

template<typename Callback>
void with_accepted_loopback_pair(const std::size_t run, Callback&& callback)
{
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    INFO(error);
    REQUIRE(server_endpoint);

    auto client_socket = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(client_socket);
    REQUIRE(client_socket->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client_socket)};
    const auto accepted = perform_handshake(
        *server,
        transport,
        *server_endpoint,
        run,
        error);
    callback(*server, transport, accepted, error);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket
require_exact_initial_request(
    const network::Datagram& request,
    const network::NetworkAddress client_endpoint)
{
    CHECK(request.source == client_endpoint);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(request.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.flags.reliable);
    CHECK_FALSE(decoded.packet->header.sequence.flags.fragmented);
    const auto exact = bytes({0x03U, 0x6eU, 0x65U, 0x77U, 0x00U});
    REQUIRE(decoded.packet->payload.size() == 8U);
    CHECK(std::ranges::equal(
        exact,
        std::span<const std::byte>{decoded.packet->payload}.first(exact.size())));
    CHECK(std::ranges::all_of(
        std::span<const std::byte>{decoded.packet->payload}.subspan(exact.size()),
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
    return std::move(*decoded.packet);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket start_and_receive_request(
    goldsrc::InitialSignonStage& stage,
    network::UdpSocket& server,
    const AcceptedPair& pair)
{
    REQUIRE(stage.start(pair.accepted_at, pair.client_endpoint));
    CHECK(stage.local_endpoint() == pair.client_endpoint);
    stage.update(pair.accepted_at + 1ms);
    auto request = receive_bounded(server, goldsrc::kMaximumNetchanDatagramSize);
    auto decoded = require_exact_initial_request(request, pair.client_endpoint);
    CHECK(decoded.header.sequence.sequence == sequence(1U));
    CHECK(stage.request_queue_count() == 1U);
    return decoded;
}

void check_success_result(
    const goldsrc::InitialSignonResult& result,
    const std::vector<std::byte>& semantic,
    const bool reassembled)
{
    CHECK(result.messages.size() == 1U);
    const auto& message = result.messages.front();
    CHECK(message.opcode == goldsrc::ServiceMessageOpcode::text_control);
    REQUIRE(std::holds_alternative<goldsrc::ServiceTextControl>(message.body));
    CHECK(std::get<goldsrc::ServiceTextControl>(message.body).text ==
          kSyntheticEarlyText);
    CHECK(result.boundary.opcode ==
          goldsrc::ServiceMessageOpcode::complex_signon_boundary);
    CHECK(result.boundary.byte_offset ==
          1U + kSyntheticEarlyText.size() + 1U);
    CHECK(result.boundary.remaining_byte_count ==
          semantic.size() - result.boundary.byte_offset - 1U);
    CHECK(result.boundary_payload.bytes == semantic);
    CHECK(result.boundary_payload.reassembled == reassembled);
    CHECK(result.boundary_payload.decompressed);
    CHECK(result.service_payload_count == 1U);
}

void check_success(
    const goldsrc::InitialSignonStage& stage,
    const std::vector<std::byte>& semantic,
    const bool reassembled)
{
    REQUIRE(stage.state() == goldsrc::InitialSignonState::signon_boundary_reached);
    REQUIRE(stage.result());
    check_success_result(*stage.result(), semantic, reassembled);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
}

void run_coordinator_baseline(const std::size_t run)
{
    INFO("production coordinator initial sign-on run " << run + 1U << "/20");
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    INFO(error);
    REQUIRE(server_endpoint);

    auto client_socket = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(client_socket);
    REQUIRE(client_socket->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client_socket)};

    goldsrc::ChallengeExchangeConfig challenge_config;
    challenge_config.retry_interval = 100ms;
    challenge_config.timeout = 1s;
    challenge_config.maximum_attempts = 2U;
    challenge_config.maximum_datagrams_per_update = 4U;
    goldsrc::ConnectResponseWaitConfig response_config;
    response_config.timeout = 1s;
    response_config.maximum_datagrams_per_update = 4U;
    std::size_t releases = 0U;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::signon_boundary,
        std::move(prepared.request),
        challenge_config,
        {},
        {},
        response_config,
        {},
        std::move(prepared.session),
        {},
        {},
        integration_config(),
        {}};

    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{static_cast<std::int64_t>(run + 1U)};
    CHECK(handshake.stop_point() == goldsrc::HandshakeStopPoint::signon_boundary);
    REQUIRE(handshake.start(epoch));
    auto challenge_request = receive_bounded(
        *server,
        goldsrc::kMaximumConnectionlessChallengeDatagramSize);
    const auto expected_challenge = goldsrc::build_getchallenge_request();
    REQUIRE(expected_challenge);
    CHECK(challenge_request.payload == *expected_challenge.datagram);
    const auto client_endpoint = challenge_request.source;
    CHECK(releases == 0U);

    constexpr std::uint32_t challenge = 0x7f00'2410U;
    send_server_datagram(
        *server,
        client_endpoint,
        challenge_response(challenge),
        error);
    handshake.update(epoch + 1ms);
    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::waiting_for_connect_response);
    auto connect = receive_bounded(*server, goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect.source == client_endpoint);
    const auto parsed = goldsrc::parse_connect_request(connect.payload, [] {
        auto profile = goldsrc::ConnectCompatibilityProfile{};
        profile.protected_authentication_is_ascii_hex = false;
        return profile;
    }());
    REQUIRE(parsed);
    CHECK(parsed.request->challenge() == challenge);
    CHECK(releases == 0U);

    send_server_datagram(
        *server,
        client_endpoint,
        accept_response(client_endpoint),
        error);
    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() == goldsrc::GoldSrcHandshakeState::waiting_for_signon);
    CHECK_FALSE(handshake.terminal());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == client_endpoint);
    REQUIRE(handshake.connect_response());
    CHECK(std::holds_alternative<goldsrc::ConnectAccepted>(
        *handshake.connect_response()));
    CHECK(handshake.connect_send_attempts() == 1U);
    CHECK_FALSE(handshake.netchan_bootstrap_result());
    CHECK_FALSE(handshake.initial_signon_result());
    CHECK(releases == 0U);
    require_no_datagram(*server);

    // The same coordinator and the same bound transport now emit the
    // client-first request; ACCEPT does not swap sockets or run bootstrap.
    handshake.update(epoch + 3ms);
    auto request_datagram = receive_bounded(
        *server,
        goldsrc::kMaximumNetchanDatagramSize);
    const auto request = require_exact_initial_request(
        request_datagram,
        client_endpoint);
    CHECK(request.header.sequence.sequence == sequence(1U));
    CHECK(request.header.acknowledgement.sequence == sequence(0U));
    CHECK_FALSE(request.header.acknowledgement.reliable);
    CHECK(releases == 0U);

    const auto semantic = semantic_service_payload(32U);
    const auto envelope = service_envelope(semantic);
    REQUIRE(envelope.size() <= goldsrc::kStockProtocol48NormalFragmentChunkSize);
    send_server_datagram(
        *server,
        client_endpoint,
        encode_server_packet(
            1U,
            true,
            request.header.sequence.sequence.value(),
            true,
            envelope),
        error);
    handshake.update(epoch + 4ms);
    auto acknowledgement = receive_bounded(
        *server,
        goldsrc::kMaximumNetchanDatagramSize);
    CHECK(acknowledgement.source == client_endpoint);
    auto decoded_ack = goldsrc::decode_client_to_server_netchan_packet(
        acknowledgement.payload);
    REQUIRE(decoded_ack);
    REQUIRE(decoded_ack.packet);
    CHECK(decoded_ack.packet->header.sequence.sequence == sequence(2U));
    CHECK_FALSE(decoded_ack.packet->header.sequence.flags.reliable);
    CHECK(decoded_ack.packet->header.acknowledgement.sequence == sequence(1U));
    CHECK(decoded_ack.packet->header.acknowledgement.reliable);

    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::signon_boundary_reached);
    CHECK(handshake.terminal());
    REQUIRE(handshake.initial_signon_result());
    check_success_result(*handshake.initial_signon_result(), semantic, false);
    CHECK(handshake.error_context().empty());
    CHECK(releases == 1U);
    require_no_datagram(*server);

    handshake.update(epoch + 5ms);
    handshake.cancel(epoch + 6ms);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::signon_boundary_reached);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

void send_fragment_and_receive_ack(
    network::UdpSocket& server,
    goldsrc::InitialSignonStage& stage,
    const AcceptedPair& pair,
    const std::span<const std::byte> envelope,
    const std::uint16_t index,
    const std::uint16_t count,
    const std::uint32_t sequence_value,
    const std::uint32_t acknowledgement,
    const bool acknowledgement_reliable,
    const goldsrc::InitialSignonTimePoint now,
    std::string& error)
{
    const auto offset = static_cast<std::size_t>(index - 1U) *
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(offset < envelope.size());
    const auto length = (std::min)(
        goldsrc::kStockProtocol48NormalFragmentChunkSize,
        envelope.size() - offset);
    send_server_datagram(
        server,
        pair.client_endpoint,
        independent_server_fragment(
            sequence_value,
            acknowledgement,
            acknowledgement_reliable,
            index,
            count,
            envelope.subspan(offset, length)),
        error);
    stage.update(now);
    auto ack = receive_bounded(server, goldsrc::kMaximumNetchanDatagramSize);
    CHECK(ack.source == pair.client_endpoint);
}

void run_fragmented(const std::size_t run, const bool drop_initial_request)
{
    INFO((drop_initial_request ? "dropped request" : "fragmented batch") <<
         " run " << run + 1U << "/20");
    with_accepted_loopback_pair(
        100U + run + (drop_initial_request ? 100U : 0U),
        [&](network::UdpSocket& server,
            network::UdpDatagramTransport& transport,
            const AcceptedPair& pair,
            std::string& error) {
            goldsrc::InitialSignonStage stage{
                transport,
                pair.server_endpoint,
                integration_config()};
            std::uint32_t initial_request_sequence = 1U;
            if (drop_initial_request) {
                REQUIRE(stage.start(pair.accepted_at, pair.client_endpoint));
                CHECK(stage.local_endpoint() == pair.client_endpoint);
                stage.update(pair.accepted_at + 1ms);
                auto dropped = receive_bounded(
                    server,
                    goldsrc::kMaximumNetchanDatagramSize);
                // Simulate loss before fake-server protocol admission. The
                // discarded C2S datagram is deliberately never netchan- or
                // client-message-decoded; only its bounded source is observed.
                CHECK(dropped.source == pair.client_endpoint);
                CHECK_FALSE(dropped.payload.empty());
                CHECK(dropped.payload.size() <=
                      goldsrc::kMaximumNetchanDatagramSize);
                CHECK(stage.request_queue_count() == 1U);
                CHECK(stage.transmitted_packet_count() == 1U);
                require_no_datagram(server);
            } else {
                const auto request = start_and_receive_request(stage, server, pair);
                initial_request_sequence =
                    request.header.sequence.sequence.value();
            }
            const auto semantic = semantic_service_payload(2'500U);
            const auto envelope = service_envelope(semantic);
            REQUIRE(envelope.size() > goldsrc::kStockProtocol48NormalFragmentChunkSize);
            const auto count_size =
                (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
                goldsrc::kStockProtocol48NormalFragmentChunkSize;
            REQUIRE(count_size >= 3U);
            REQUIRE(count_size <= (std::numeric_limits<std::uint16_t>::max)());
            const auto count = static_cast<std::uint16_t>(count_size);

            // Final first establishes the project out-of-order proof. In the
            // dropped-request scenario its old generation produces an exact
            // driver-owned retransmission without a second semantic queue.
            const auto final_offset = static_cast<std::size_t>(count - 1U) *
                goldsrc::kStockProtocol48NormalFragmentChunkSize;
            const auto final_datagram = independent_server_fragment(
                1U,
                initial_request_sequence,
                !drop_initial_request,
                count,
                count,
                std::span<const std::byte>{envelope}.subspan(final_offset));
            send_server_datagram(
                server,
                pair.client_endpoint,
                final_datagram,
                error);
            stage.update(pair.accepted_at + 2ms);

            std::uint32_t request_ack_sequence = initial_request_sequence;
            std::uint16_t next_fragment_index = 1U;
            std::uint32_t server_sequence = 2U;
            if (drop_initial_request) {
                auto first_ack = receive_bounded(
                    server,
                    goldsrc::kMaximumNetchanDatagramSize);
                CHECK(first_ack.source == pair.client_endpoint);
                auto first_ack_decoded =
                    goldsrc::decode_client_to_server_netchan_packet(
                        first_ack.payload);
                REQUIRE(first_ack_decoded);
                REQUIRE(first_ack_decoded.packet);
                CHECK_FALSE(first_ack_decoded.packet->header.sequence.flags.reliable);

                // The numeric ACK now covers the client's first fragment ACK
                // (sequence 2) with the old request generation, creating the
                // established ACK-gap retry trigger without a semantic requeue.
                send_server_datagram(
                    server,
                    pair.client_endpoint,
                    independent_server_fragment(
                        2U,
                        first_ack_decoded.packet->header.sequence.sequence.value(),
                        false,
                        1U,
                        count,
                        std::span<const std::byte>{envelope}.first(
                            goldsrc::kStockProtocol48NormalFragmentChunkSize)),
                    error);
                stage.update(pair.accepted_at + 3ms);
                auto retry = receive_bounded(
                    server,
                    goldsrc::kMaximumNetchanDatagramSize);
                std::size_t admitted_request_count = 0U;
                const auto retry_decoded = require_exact_initial_request(
                    retry,
                    pair.client_endpoint);
                ++admitted_request_count;
                CHECK(retry_decoded.header.sequence.sequence !=
                      sequence(initial_request_sequence));
                request_ack_sequence =
                    retry_decoded.header.sequence.sequence.value();
                CHECK(admitted_request_count == 1U);
                CHECK(stage.request_queue_count() == 1U);
                REQUIRE(stage.driver());
                REQUIRE(stage.driver()->session().in_flight_reliable_payload());
                CHECK(stage.driver()->session().in_flight_reliable_payload()->
                          send_count == 2U);
                next_fragment_index = 2U;
                server_sequence = 3U;
            } else {
                auto ack = receive_bounded(
                    server,
                    goldsrc::kMaximumNetchanDatagramSize);
                CHECK(ack.source == pair.client_endpoint);

                // Exact same-sequence duplicate is ignored before reassembly
                // and publishes neither a second ACK nor semantic event.
                send_server_datagram(
                    server,
                    pair.client_endpoint,
                    final_datagram,
                    error);
                stage.update(pair.accepted_at + 3ms);
                require_no_datagram(server);
            }

            auto now = pair.accepted_at + 4ms;
            for (std::uint16_t index = next_fragment_index;
                 index < count;
                 ++index) {
                send_fragment_and_receive_ack(
                    server,
                    stage,
                    pair,
                    envelope,
                    index,
                    count,
                    server_sequence,
                    request_ack_sequence,
                    true,
                    now,
                    error);
                ++server_sequence;
                now += 1ms;
            }

            check_success(stage, semantic, true);
            CHECK(stage.transmitted_packet_count() ==
                  1U + count);
            require_no_datagram(server);
            const auto sent_at_boundary = stage.transmitted_packet_count();
            stage.update(now + 1ms);
            CHECK(stage.transmitted_packet_count() == sent_at_boundary);
            require_no_datagram(server);
        });
}

} // namespace

TEST_CASE("Fake HLDS production coordinator sign-on boundary repeats 20 of 20",
          "[goldsrc][signon][udp][coordinator][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_coordinator_baseline(run);
    }
}

TEST_CASE("Fake HLDS dropped initial request repeats 20 of 20 without semantic requeue",
          "[goldsrc][signon][udp][drop-request][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_fragmented(run, true);
    }
}

TEST_CASE("Fake HLDS fragmented BZ2 service boundary repeats 20 of 20",
          "[goldsrc][signon][udp][fragment][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_fragmented(run, false);
    }
}
