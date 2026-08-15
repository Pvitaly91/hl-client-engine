#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_driver.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
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

inline constexpr std::string_view kSyntheticAuthenticationMarker =
    "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
inline constexpr std::string_view kFragmentedPayloadMarker =
    "FRAGMENTED_NETCHAN_TEST_PAYLOAD";
inline constexpr std::string_view kOutgoingPayloadMarker =
    "OUTGOING_FRAGMENTED_NETCHAN_TEST_PAYLOAD";

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

[[nodiscard]] std::uint16_t read_u16_le(
    const std::span<const std::byte> input,
    const std::size_t offset)
{
    REQUIRE(offset <= input.size());
    REQUIRE(input.size() - offset >= 2U);
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(input[offset]) |
        (static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(input[offset + 1U]))
         << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::span<const std::byte> input,
    const std::size_t offset)
{
    REQUIRE(offset <= input.size());
    REQUIRE(input.size() - offset >= 4U);
    return std::to_integer<std::uint8_t>(input[offset]) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(input[offset + 1U]))
            << 8U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(input[offset + 2U]))
            << 16U) |
           (static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(input[offset + 3U]))
            << 24U);
}

[[nodiscard]] const std::array<std::uint8_t, 16U>& exact_first_acknowledgement()
{
    static constexpr std::array<std::uint8_t, 16U> fixture{
        0x01U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x80U,
        0x5aU, 0x19U, 0x01U, 0x00U,
        0x1aU, 0x01U, 0x11U, 0x40U,
    };
    return fixture;
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
    throw std::runtime_error{"Timed out waiting for bounded fake-HLDS traffic"};
}

void require_no_datagram(network::UdpSocket& socket)
{
    const auto received = socket.receive(goldsrc::kMaximumNetchanDatagramSize);
    if (received.status == network::ReceiveStatus::received) {
        FAIL("Unexpected extra fake-HLDS integration datagram");
    }
    if (received.status == network::ReceiveStatus::error ||
        received.status == network::ReceiveStatus::truncated) {
        FAIL(received.error);
    }
    CHECK(received.status == network::ReceiveStatus::would_block);
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

// Independent fake-HLDS descriptor fixture. Production fragment encode and
// reassembly APIs are deliberately not used to construct incoming traffic.
[[nodiscard]] std::vector<std::byte> independent_server_fragment(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    const std::span<const std::byte> fragment,
    const std::uint8_t slot = 0U)
{
    REQUIRE(slot < goldsrc::kNetchanFragmentSlotCount);
    REQUIRE_FALSE(fragment.empty());
    REQUIRE(fragment.size() <= goldsrc::kStockProtocol48NormalFragmentChunkSize);
    std::vector<std::byte> body;
    for (std::uint8_t current = 0U;
         current < goldsrc::kNetchanFragmentSlotCount;
         ++current) {
        body.push_back(std::byte{current == slot ? 1U : 0U});
        if (current == slot) {
            append_u32_le(
                body,
                (static_cast<std::uint32_t>(fragment_index) << 16U) |
                    static_cast<std::uint32_t>(fragment_count));
            append_u16_le(body, 0U);
            append_u16_le(body, static_cast<std::uint16_t>(fragment.size()));
        }
    }
    body.insert(body.end(), fragment.begin(), fragment.end());
    goldsrc::encode_netchan_payload(body, sequence(packet_sequence));

    std::vector<std::byte> datagram;
    append_u32_le(
        datagram,
        packet_sequence | goldsrc::kNetchanReliableSequenceFlag |
            goldsrc::kNetchanFragmentSequenceFlag);
    append_u32_le(datagram, acknowledgement);
    datagram.insert(datagram.end(), body.begin(), body.end());
    return datagram;
}

struct IndependentlyDecodedFragment {
    std::uint32_t packet_sequence{0U};
    std::uint16_t fragment_index{0U};
    std::uint16_t fragment_count{0U};
    std::uint16_t descriptor_offset{0U};
    std::vector<std::byte> canonical_bytes;
};

// Independent fake-server validation of outgoing descriptor layout. The
// production packet/fragment decoder is deliberately not used.
[[nodiscard]] IndependentlyDecodedFragment decode_client_fragment(
    const std::span<const std::byte> datagram)
{
    REQUIRE(datagram.size() >= goldsrc::kNetchanHeaderSize + 10U);
    const auto raw_sequence = read_u32_le(datagram, 0U);
    REQUIRE((raw_sequence & goldsrc::kNetchanReliableSequenceFlag) != 0U);
    REQUIRE((raw_sequence & goldsrc::kNetchanFragmentSequenceFlag) != 0U);
    const auto packet_sequence = raw_sequence & goldsrc::kNetchanSequenceMask;
    std::vector<std::byte> decoded{
        datagram.begin() + static_cast<std::ptrdiff_t>(goldsrc::kNetchanHeaderSize),
        datagram.end()};
    goldsrc::decode_netchan_payload(decoded, sequence(packet_sequence));

    REQUIRE(std::to_integer<std::uint8_t>(decoded[0U]) == 1U);
    const auto packed = read_u32_le(decoded, 1U);
    const auto descriptor_offset = read_u16_le(decoded, 5U);
    const auto length = read_u16_le(decoded, 7U);
    REQUIRE(std::to_integer<std::uint8_t>(decoded[9U]) == 0U);
    REQUIRE(descriptor_offset == 0U);
    REQUIRE(decoded.size() - 10U >= length);
    REQUIRE(decoded.size() - 10U == length);
    return IndependentlyDecodedFragment{
        packet_sequence,
        static_cast<std::uint16_t>(packed >> 16U),
        static_cast<std::uint16_t>(packed & 0xffffU),
        descriptor_offset,
        std::vector<std::byte>{
            decoded.begin() + 10,
            decoded.begin() + 10 + static_cast<std::ptrdiff_t>(length)},
    };
}

[[nodiscard]] std::vector<std::byte> incoming_canonical_payload()
{
    const auto marker = bytes(kFragmentedPayloadMarker);
    std::vector<std::byte> canonical(
        goldsrc::kStockProtocol48NormalFragmentChunkSize,
        std::byte{0x01});
    canonical.insert(canonical.end(), marker.begin(), marker.end());
    return canonical;
}

[[nodiscard]] std::vector<std::byte> outgoing_canonical_payload()
{
    std::vector<std::byte> canonical(
        2U * goldsrc::kStockProtocol48NormalFragmentChunkSize,
        std::byte{0x4f});
    const auto marker = bytes(kOutgoingPayloadMarker);
    canonical.insert(canonical.end(), marker.begin(), marker.end());
    return canonical;
}

template<typename Callback>
void with_accepted_loopback_pair(const std::size_t run, Callback&& callback)
{
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error;
    auto server_socket = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server_socket);
    REQUIRE(server_socket->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server_socket->local_address(error);
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
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::connect_response,
        prepared_request(),
        challenge_config,
        {},
        {},
        response_config};

    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{static_cast<std::int64_t>(run + 1U)};
    REQUIRE(handshake.start(epoch));
    auto challenge_request = receive_bounded(
        *server_socket,
        goldsrc::kMaximumConnectionlessChallengeDatagramSize);
    const auto expected_challenge = goldsrc::build_getchallenge_request();
    REQUIRE(expected_challenge);
    CHECK(challenge_request.payload == *expected_challenge.datagram);
    const auto client_endpoint = challenge_request.source;

    constexpr std::uint32_t challenge = 0x7f00'2303U;
    REQUIRE(server_socket->send_to(
        client_endpoint,
        challenge_response(challenge),
        error));
    handshake.update(epoch + 1ms);
    auto connect = receive_bounded(
        *server_socket,
        goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect.source == client_endpoint);
    const auto parsed = goldsrc::parse_connect_request(connect.payload, [] {
        auto profile = goldsrc::ConnectCompatibilityProfile{};
        profile.protected_authentication_is_ascii_hex = false;
        return profile;
    }());
    REQUIRE(parsed);
    CHECK(parsed.request->challenge() == challenge);

    REQUIRE(server_socket->send_to(
        client_endpoint,
        accept_response(client_endpoint),
        error));
    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() == goldsrc::GoldSrcHandshakeState::accepted);
    REQUIRE(handshake.terminal());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == client_endpoint);
    REQUIRE(handshake.connect_response());
    CHECK(std::holds_alternative<goldsrc::ConnectAccepted>(
        *handshake.connect_response()));
    CHECK(handshake.connect_send_attempts() == 1U);

    callback(
        *server_socket,
        transport,
        *server_endpoint,
        client_endpoint,
        epoch + 2ms,
        error);
}

[[nodiscard]] goldsrc::NetchanDriverConfig integration_config()
{
    goldsrc::NetchanDriverConfig config;
    config.channel_inactivity_timeout = 500ms;
    config.fragment_transfer_timeout = 50ms;
    config.maximum_datagram_size = 1'100U;
    config.maximum_fragment_datagram_size = 1'100U;
    config.maximum_unreliable_payload_size =
        config.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    config.maximum_datagrams_per_update = 8U;
    config.maximum_outgoing_packets_per_update = 1U;
    config.maximum_events = 16U;
    return config;
}

void send_server_datagram(
    network::UdpSocket& server,
    const network::NetworkAddress client,
    const std::span<const std::byte> datagram,
    std::string& error)
{
    REQUIRE(server.send_to(client, datagram, error));
}

void run_incoming_fragment_transfer(const std::size_t run)
{
    INFO("incoming repeated run " << run + 1U << "/20");
    with_accepted_loopback_pair(
        run,
        [&](network::UdpSocket& server,
            network::UdpDatagramTransport& transport,
            const network::NetworkAddress server_endpoint,
            const network::NetworkAddress client_endpoint,
            const goldsrc::NetchanDriverTimePoint accepted_at,
            std::string& error) {
            goldsrc::NetchanDriver driver{
                transport,
                server_endpoint,
                integration_config()};
            REQUIRE(driver.start(accepted_at, client_endpoint));
            CHECK(driver.local_endpoint() == client_endpoint);

            const auto canonical = incoming_canonical_payload();
            const auto final = std::span<const std::byte>{canonical}.subspan(
                goldsrc::kStockProtocol48NormalFragmentChunkSize);
            const auto first = std::span<const std::byte>{canonical}.first(
                goldsrc::kStockProtocol48NormalFragmentChunkSize);
            const auto final_first = independent_server_fragment(
                1U,
                0U,
                2U,
                2U,
                final);
            send_server_datagram(server, client_endpoint, final_first, error);
            driver.update(accepted_at + 1ms);

            auto acknowledgement = receive_bounded(
                server,
                goldsrc::kMaximumNetchanDatagramSize);
            CHECK(acknowledgement.source == client_endpoint);
            CHECK(acknowledgement.payload == bytes(exact_first_acknowledgement()));
            CHECK(driver.pending_event_count() == 1U);

            // Exact same-sequence duplicate is rejected before body/reassembly
            // state and produces neither another ACK nor another event.
            send_server_datagram(server, client_endpoint, final_first, error);
            driver.update(accepted_at + 2ms);
            require_no_datagram(server);
            CHECK(driver.pending_event_count() == 1U);

            send_server_datagram(
                server,
                client_endpoint,
                independent_server_fragment(2U, 1U, 1U, 2U, first),
                error);
            driver.update(accepted_at + 3ms);
            acknowledgement = receive_bounded(
                server,
                goldsrc::kMaximumNetchanDatagramSize);
            CHECK(acknowledgement.source == client_endpoint);
            const auto decoded_ack =
                goldsrc::decode_client_to_server_netchan_packet(
                    acknowledgement.payload);
            REQUIRE(decoded_ack);
            REQUIRE(decoded_ack.packet);
            CHECK(decoded_ack.packet->header.acknowledgement.sequence ==
                  sequence(2U));
            CHECK_FALSE(decoded_ack.packet->header.acknowledgement.reliable);

            std::size_t completion_count = 0U;
            std::size_t payload_count = 0U;
            std::vector<std::byte> delivered;
            while (auto event = driver.poll_event()) {
                if (event->type ==
                    goldsrc::NetchanDriverEventType::normal_transfer_completed) {
                    ++completion_count;
                    CHECK(event->transfer_size == canonical.size());
                }
                if (event->type == goldsrc::NetchanDriverEventType::payload_ready &&
                    event->payload) {
                    ++payload_count;
                    delivered = std::move(event->payload->bytes);
                }
            }
            CHECK(completion_count == 1U);
            CHECK(payload_count == 1U);
            CHECK(delivered == canonical);
            CHECK(std::ranges::equal(
                bytes(kFragmentedPayloadMarker),
                std::span<const std::byte>{delivered}.last(
                    kFragmentedPayloadMarker.size())));
            CHECK(driver.session().state().incoming_sequence == sequence(2U));
            require_no_datagram(server);
            driver.close(accepted_at + 4ms);
            CHECK(driver.cleanup_count() == 1U);
        });
}

void run_outgoing_fragment_transfer(const std::size_t run)
{
    INFO("outgoing repeated run " << run + 1U << "/20");
    with_accepted_loopback_pair(
        run + 100U,
        [&](network::UdpSocket& server,
            network::UdpDatagramTransport& transport,
            const network::NetworkAddress server_endpoint,
            const network::NetworkAddress client_endpoint,
            const goldsrc::NetchanDriverTimePoint accepted_at,
            std::string& error) {
            goldsrc::NetchanDriver driver{
                transport,
                server_endpoint,
                integration_config()};
            REQUIRE(driver.start(accepted_at, client_endpoint));

            send_server_datagram(
                server,
                client_endpoint,
                encode_server_packet(
                    1U,
                    true,
                    0U,
                    false,
                    bytes("BOOTSTRAP")),
                error);
            driver.update(accepted_at + 1ms);
            auto client_datagram = receive_bounded(
                server,
                goldsrc::kMaximumNetchanDatagramSize);
            CHECK(client_datagram.source == client_endpoint);
            CHECK(client_datagram.payload == bytes(exact_first_acknowledgement()));

            const auto canonical = outgoing_canonical_payload();
            REQUIRE(driver.queue_reliable(canonical));
            driver.update(accepted_at + 2ms);
            client_datagram = receive_bounded(
                server,
                goldsrc::kMaximumNetchanDatagramSize);
            CHECK(client_datagram.source == client_endpoint);
            const auto dropped_first = decode_client_fragment(
                client_datagram.payload);
            CHECK(dropped_first.fragment_index == 1U);
            CHECK(dropped_first.fragment_count == 3U);
            CHECK(dropped_first.canonical_bytes ==
                  std::vector<std::byte>{
                      canonical.begin(),
                      canonical.begin() + static_cast<std::ptrdiff_t>(
                          goldsrc::kStockProtocol48NormalFragmentChunkSize)});

            // Stock-confirmed policy: a numeric ACK gap with the old reliable
            // generation, not a timer, requests the exact missing unit again.
            REQUIRE(driver.submit_unreliable(bytes("ACK_GAP_PROBE")));
            driver.update(accepted_at + 3ms);
            const auto probe = receive_bounded(
                server,
                goldsrc::kMaximumNetchanDatagramSize);
            CHECK(probe.source == client_endpoint);
            const auto probe_sequence =
                read_u32_le(probe.payload, 0U) & goldsrc::kNetchanSequenceMask;
            CHECK(probe_sequence > dropped_first.packet_sequence);

            send_server_datagram(
                server,
                client_endpoint,
                encode_server_packet(
                    2U,
                    false,
                    probe_sequence,
                    false,
                    {}),
                error);
            driver.update(accepted_at + 4ms);
            client_datagram = receive_bounded(
                server,
                goldsrc::kMaximumNetchanDatagramSize);
            CHECK(client_datagram.source == client_endpoint);
            const auto retry = decode_client_fragment(client_datagram.payload);
            CHECK(retry.packet_sequence != dropped_first.packet_sequence);
            CHECK(retry.fragment_index == dropped_first.fragment_index);
            CHECK(retry.fragment_count == dropped_first.fragment_count);
            CHECK(retry.canonical_bytes == dropped_first.canonical_bytes);

            std::vector<std::byte> reconstructed;
            reconstructed.reserve(canonical.size());
            reconstructed.insert(
                reconstructed.end(),
                retry.canonical_bytes.begin(),
                retry.canonical_bytes.end());
            std::uint32_t server_sequence = 3U;
            std::uint32_t acknowledged_client_sequence = retry.packet_sequence;
            bool acknowledged_generation = true;
            for (std::uint16_t expected_index = 2U;
                 expected_index <= 3U;
                 ++expected_index) {
                send_server_datagram(
                    server,
                    client_endpoint,
                    encode_server_packet(
                        server_sequence,
                        false,
                        acknowledged_client_sequence,
                        acknowledged_generation,
                        {}),
                    error);
                driver.update(
                    accepted_at +
                    std::chrono::milliseconds{server_sequence + 2U});
                client_datagram = receive_bounded(
                    server,
                    goldsrc::kMaximumNetchanDatagramSize);
                CHECK(client_datagram.source == client_endpoint);
                const auto fragment = decode_client_fragment(
                    client_datagram.payload);
                CHECK(fragment.fragment_index == expected_index);
                CHECK(fragment.fragment_count == 3U);
                reconstructed.insert(
                    reconstructed.end(),
                    fragment.canonical_bytes.begin(),
                    fragment.canonical_bytes.end());
                acknowledged_client_sequence = fragment.packet_sequence;
                acknowledged_generation = !acknowledged_generation;
                ++server_sequence;
            }

            send_server_datagram(
                server,
                client_endpoint,
                encode_server_packet(
                    server_sequence,
                    false,
                    acknowledged_client_sequence,
                    acknowledged_generation,
                    {}),
                error);
            driver.update(accepted_at + 8ms);
            CHECK(reconstructed == canonical);
            CHECK(std::ranges::equal(
                bytes(kOutgoingPayloadMarker),
                std::span<const std::byte>{reconstructed}.last(
                    kOutgoingPayloadMarker.size())));
            CHECK_FALSE(driver.session().in_flight_reliable_payload());
            CHECK_FALSE(driver.session().outgoing_fragment_transfer());
            CHECK(driver.session().pending_reliable_payload().empty());

            std::size_t acknowledged_events = 0U;
            while (auto event = driver.poll_event()) {
                if (event->type ==
                    goldsrc::NetchanDriverEventType::
                        reliable_payload_acknowledged) {
                    ++acknowledged_events;
                }
            }
            CHECK(acknowledged_events == 1U);
            CHECK(driver.transmitted_packet_count() == 6U);
            require_no_datagram(server);
            driver.close(accepted_at + 9ms);
            CHECK(driver.cleanup_count() == 1U);
        });
}

} // namespace

TEST_CASE("Fake HLDS incoming fragmented transfer repeats 20 of 20 on one socket",
          "[goldsrc][netchan][fragment][udp][incoming][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_incoming_fragment_transfer(run);
    }
}

TEST_CASE("Fake HLDS outgoing fragmented transfer repeats 20 of 20 on one socket",
          "[goldsrc][netchan][fragment][udp][outgoing][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_outgoing_fragment_transfer(run);
    }
}

TEST_CASE("Fake HLDS missing fragment times out without partial delivery",
          "[goldsrc][netchan][fragment][udp][timeout]")
{
    with_accepted_loopback_pair(
        1'000U,
        [](network::UdpSocket& server,
           network::UdpDatagramTransport& transport,
           const network::NetworkAddress server_endpoint,
           const network::NetworkAddress client_endpoint,
           const goldsrc::NetchanDriverTimePoint accepted_at,
           std::string& error) {
            auto config = integration_config();
            config.fragment_transfer_timeout = 10ms;
            goldsrc::NetchanDriver driver{transport, server_endpoint, config};
            REQUIRE(driver.start(accepted_at, client_endpoint));
            const std::vector<std::byte> first(
                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                std::byte{0x4d});
            send_server_datagram(
                server,
                client_endpoint,
                independent_server_fragment(1U, 0U, 1U, 2U, first),
                error);
            driver.update(accepted_at + 1ms);
            const auto acknowledgement = receive_bounded(
                server,
                goldsrc::kMaximumNetchanDatagramSize);
            CHECK(acknowledgement.source == client_endpoint);
            CHECK(acknowledgement.payload == bytes(exact_first_acknowledgement()));

            driver.update(accepted_at + 11ms);
            CHECK(driver.state() == goldsrc::NetchanDriverState::timed_out);
            REQUIRE(driver.last_error());
            CHECK(driver.last_error()->code ==
                  goldsrc::NetchanDriverErrorCode::fragment_transfer_timed_out);
            std::size_t timeout_events = 0U;
            std::size_t payload_events = 0U;
            while (auto event = driver.poll_event()) {
                timeout_events += event->type ==
                    goldsrc::NetchanDriverEventType::normal_transfer_timed_out;
                payload_events += event->type ==
                    goldsrc::NetchanDriverEventType::payload_ready;
            }
            CHECK(timeout_events == 1U);
            CHECK(payload_events == 0U);
            CHECK_FALSE(driver.normal_reassembler().active_transfer());
            CHECK(driver.normal_reassembler().ranges().empty());
            require_no_datagram(server);
        });
}

TEST_CASE("Fake HLDS secondary stream is typed pending-M3 and never persisted",
          "[goldsrc][netchan][fragment][udp][secondary][file-boundary]")
{
    with_accepted_loopback_pair(
        1'001U,
        [](network::UdpSocket& server,
           network::UdpDatagramTransport& transport,
           const network::NetworkAddress server_endpoint,
           const network::NetworkAddress client_endpoint,
           const goldsrc::NetchanDriverTimePoint accepted_at,
           std::string& error) {
            goldsrc::NetchanDriver driver{
                transport,
                server_endpoint,
                integration_config()};
            REQUIRE(driver.start(accepted_at, client_endpoint));
            const auto forbidden = bytes("REMOTE_STREAM_BYTES");
            send_server_datagram(
                server,
                client_endpoint,
                independent_server_fragment(
                    1U,
                    0U,
                    1U,
                    1U,
                    forbidden,
                    1U),
                error);
            driver.update(accepted_at + 1ms);
            CHECK(driver.state() == goldsrc::NetchanDriverState::protocol_error);
            REQUIRE(driver.last_error());
            CHECK(driver.last_error()->code ==
                  goldsrc::NetchanDriverErrorCode::secondary_stream_pending_m3);
            std::size_t pending_events = 0U;
            std::size_t payload_events = 0U;
            while (auto event = driver.poll_event()) {
                pending_events += event->type ==
                    goldsrc::NetchanDriverEventType::secondary_stream_pending_m3;
                payload_events += event->type ==
                    goldsrc::NetchanDriverEventType::payload_ready;
            }
            CHECK(pending_events == 1U);
            CHECK(payload_events == 0U);
            CHECK(driver.session().state().incoming_sequence == sequence(0U));
            CHECK_FALSE(driver.normal_reassembler().active_transfer());
            require_no_datagram(server);
        });
}
