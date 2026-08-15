#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/goldsrc/netchan_session.hpp>
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

namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

inline constexpr std::string_view kSyntheticAuthenticationMarker =
    "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
inline constexpr std::string_view kBootstrapPayload =
    "NETCHAN_BOOTSTRAP_TEST_PAYLOAD";
inline constexpr std::string_view kClientReliablePayload =
    "RELIABLE_CLIENT_TEST";
inline constexpr std::string_view kServerReliablePayload =
    "RELIABLE_SERVER_TEST";

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

[[nodiscard]] std::vector<std::byte> padding_body()
{
    return std::vector<std::byte>(
        goldsrc::kStockProtocol48MinimumDecodedPayloadSize,
        goldsrc::kStockProtocol48NetchanPaddingByte);
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
    throw std::runtime_error{"Timed out waiting for a bounded loopback datagram"};
}

[[nodiscard]] network::Datagram receive_bounded(
    network::UdpDatagramTransport& transport,
    const std::size_t maximum_size)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto received = transport.receive(maximum_size);
        if (received.status == network::DatagramTransportReceiveStatus::received) {
            REQUIRE(received.datagram);
            return std::move(*received.datagram);
        }
        if (received.status == network::DatagramTransportReceiveStatus::error ||
            received.status == network::DatagramTransportReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::yield();
    }
    throw std::runtime_error{"Timed out waiting for a bounded transported datagram"};
}

void check_no_datagram(
    network::UdpSocket& socket,
    const std::chrono::milliseconds observation_window = 10ms)
{
    const auto deadline = std::chrono::steady_clock::now() + observation_window;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto received = socket.receive(goldsrc::kMaximumNetchanDatagramSize);
        if (received.status == network::ReceiveStatus::received) {
            FAIL("Unexpected automatic netchan transmission");
        }
        if (received.status == network::ReceiveStatus::error ||
            received.status == network::ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::yield();
    }
}

[[nodiscard]] std::vector<std::byte> encode_server_packet(
    const std::uint32_t sequence_value,
    const bool reliable_present,
    const std::uint32_t acknowledgement_value,
    const bool reliable_acknowledgement,
    std::vector<std::byte> payload)
{
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(sequence_value),
                goldsrc::NetchanSequenceFlags{reliable_present, false},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement_value),
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

[[nodiscard]] std::vector<std::byte> fragmented_server_packet(
    const std::uint32_t sequence_value,
    const std::uint32_t acknowledgement_value,
    const bool reliable_acknowledgement,
    const bool malformed)
{
    std::vector<std::byte> datagram;
    datagram.reserve(22U);
    append_u32_le(
        datagram,
        sequence_value | goldsrc::kNetchanReliableSequenceFlag |
            goldsrc::kNetchanFragmentSequenceFlag);
    append_u32_le(
        datagram,
        acknowledgement_value |
            (reliable_acknowledgement
                 ? goldsrc::kNetchanReliableAcknowledgementFlag
                 : 0U));
    datagram.push_back(std::byte{0x01}); // slot 0 present
    if (!malformed) {
        append_u32_le(datagram, 0x0001'0001U);
        datagram.push_back(std::byte{0x00}); // offset 0
        datagram.push_back(std::byte{0x00});
        datagram.push_back(std::byte{0x04}); // four-byte fragment
        datagram.push_back(std::byte{0x00});
        datagram.push_back(std::byte{0x00}); // slot 1 absent
        datagram.insert(
            datagram.end(),
            {std::byte{0x90}, std::byte{0x91}, std::byte{0x92}, std::byte{0x93}});
    }
    goldsrc::encode_netchan_payload(
        std::span<std::byte>{datagram}.subspan(goldsrc::kNetchanHeaderSize),
        sequence(sequence_value));
    return datagram;
}

enum class DriverDisposition {
    delivered,
    duplicate_ignored,
    older_ignored,
    wrong_endpoint_ignored,
    fragment_not_processed_by_m232_fixture,
    malformed,
    protocol_error,
};

struct DriverResult {
    DriverDisposition disposition{DriverDisposition::protocol_error};
    bool contains_new_reliable_data{false};
    std::optional<std::vector<std::byte>> payload;
};

[[nodiscard]] DriverResult process_server_datagram(
    goldsrc::NetchanSession& session,
    const network::NetworkAddress source,
    const network::NetworkAddress expected_source,
    const std::span<const std::byte> datagram)
{
    if (source != expected_source) {
        return DriverResult{DriverDisposition::wrong_endpoint_ignored};
    }

    const auto peeked = goldsrc::peek_netchan_header(datagram);
    if (!peeked || !peeked.packet) {
        return DriverResult{DriverDisposition::malformed};
    }

    const auto sequence_comparison = goldsrc::compare_sequences(
        peeked.packet->header.sequence.sequence,
        session.state().incoming_sequence);
    if (sequence_comparison == goldsrc::NetchanSequenceComparison::equal) {
        return DriverResult{DriverDisposition::duplicate_ignored};
    }
    if (sequence_comparison == goldsrc::NetchanSequenceComparison::older) {
        return DriverResult{DriverDisposition::older_ignored};
    }
    if (sequence_comparison ==
        goldsrc::NetchanSequenceComparison::half_range_ambiguous) {
        return DriverResult{DriverDisposition::protocol_error};
    }

    auto decoded = goldsrc::decode_server_to_client_netchan_packet(datagram);
    if (!decoded || !decoded.packet) {
        return DriverResult{DriverDisposition::malformed};
    }
    if (decoded.packet->header.sequence.flags.fragmented) {
        return DriverResult{
            DriverDisposition::fragment_not_processed_by_m232_fixture};
    }

    auto inspected = session.inspect_incoming(decoded.packet->header);
    if (!inspected || !inspected.inspection ||
        !inspected.inspection->should_commit()) {
        return DriverResult{DriverDisposition::protocol_error};
    }
    const bool contains_reliable =
        inspected.inspection->contains_new_reliable_data();
    auto payload = std::move(decoded.packet->payload);
    const auto committed = session.commit_incoming(
        std::move(*inspected.inspection));
    if (!committed) {
        return DriverResult{DriverDisposition::protocol_error};
    }
    return DriverResult{
        DriverDisposition::delivered,
        contains_reliable,
        std::move(payload),
    };
}

[[nodiscard]] goldsrc::NetchanSequenceState post_bootstrap_state()
{
    return goldsrc::NetchanSequenceState{
        sequence(2U),
        sequence(1U),
        sequence(1U),
        sequence(0U),
        true,
        false,
    };
}

struct SessionSnapshot {
    std::uint32_t next_outgoing{0U};
    std::uint32_t last_outgoing{0U};
    std::uint32_t incoming{0U};
    std::uint32_t peer_acknowledgement{0U};
    bool incoming_reliable_acknowledgement{false};
    bool peer_reliable_acknowledgement{false};
    bool outgoing_reliable_toggle{false};
    std::vector<std::byte> pending;
    std::vector<std::byte> in_flight;
    std::optional<std::uint32_t> first_sent;
    std::optional<std::uint32_t> latest_sent;
    std::uint64_t send_count{0U};
    bool retransmission_requested{false};

    [[nodiscard]] friend bool operator==(
        const SessionSnapshot&,
        const SessionSnapshot&) = default;
};

[[nodiscard]] SessionSnapshot snapshot(const goldsrc::NetchanSession& session)
{
    SessionSnapshot result{
        session.state().next_outgoing_sequence.value(),
        session.state().last_outgoing_sequence.value(),
        session.state().incoming_sequence.value(),
        session.state().peer_acknowledgement.value(),
        session.state().incoming_reliable_acknowledgement,
        session.state().peer_reliable_acknowledgement,
        session.outgoing_reliable_toggle(),
        session.pending_reliable_payload(),
    };
    if (session.in_flight_reliable_payload()) {
        const auto& in_flight = *session.in_flight_reliable_payload();
        result.in_flight = in_flight.bytes;
        result.first_sent = in_flight.first_sent_sequence.value();
        result.latest_sent = in_flight.most_recent_sent_sequence.value();
        result.send_count = in_flight.send_count;
        result.retransmission_requested = in_flight.retransmission_requested;
    }
    return result;
}

[[nodiscard]] goldsrc::NetchanTransmitPlan prepare_outgoing(
    const goldsrc::NetchanSession& session,
    const std::span<const std::byte> unreliable = {})
{
    auto prepared = session.prepare_outgoing_packet(unreliable);
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    return std::move(*prepared.plan);
}

void commit_outgoing(
    goldsrc::NetchanSession& session,
    goldsrc::NetchanTransmitPlan& plan)
{
    REQUIRE(session.commit_outgoing_send(std::move(plan)));
}

void commit_server_header(
    goldsrc::NetchanSession& session,
    const std::uint32_t sequence_value,
    const std::uint32_t acknowledgement_value,
    const bool reliable_acknowledgement)
{
    const goldsrc::NetchanHeader header{
        goldsrc::NetchanSequenceWord{
            sequence(sequence_value),
            goldsrc::NetchanSequenceFlags{false, false},
        },
        goldsrc::NetchanAcknowledgementWord{
            sequence(acknowledgement_value),
            reliable_acknowledgement,
        },
    };
    auto inspected = session.inspect_incoming(header);
    REQUIRE(inspected);
    REQUIRE(inspected.inspection);
    REQUIRE(inspected.inspection->should_commit());
    REQUIRE(session.commit_incoming(std::move(*inspected.inspection)));
}

TEST_CASE(
    "Fake HLDS reuses the bootstrap socket and session for reliable traffic",
    "[goldsrc][netchan][reliable][udp][integration]")
{
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());

    std::string error;
    auto server_socket = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server_socket);
    REQUIRE(server_socket->bind(network::NetworkAddress::loopback(0), error));
    const auto server_endpoint = server_socket->local_address(error);
    INFO(error);
    REQUIRE(server_endpoint);

    auto client_socket = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(client_socket);
    REQUIRE(client_socket->bind(network::NetworkAddress::loopback(0), error));
    network::UdpDatagramTransport transport{std::move(*client_socket)};

    goldsrc::ChallengeExchangeConfig challenge_config;
    challenge_config.retry_interval = 100ms;
    challenge_config.timeout = 1s;
    challenge_config.maximum_attempts = 2U;
    challenge_config.maximum_datagrams_per_update = 4U;
    goldsrc::ConnectResponseWaitConfig response_config;
    response_config.timeout = 1s;
    response_config.maximum_datagrams_per_update = 4U;
    goldsrc::NetchanBootstrapConfig netchan_config;
    netchan_config.first_packet_timeout = 1s;
    netchan_config.maximum_datagrams_per_update = 4U;
    netchan_config.maximum_outgoing_packets_per_update = 1U;

    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::netchan_bootstrap,
        prepared_request(),
        challenge_config,
        {},
        {},
        response_config,
        {},
        std::nullopt,
        netchan_config,
    };
    CHECK(handshake.netchan_session() == nullptr);

    const auto getchallenge = goldsrc::build_getchallenge_request();
    REQUIRE(getchallenge);
    constexpr std::uint32_t challenge = 0xf000'0001U;
    const auto epoch = goldsrc::ChallengeExchangeClock::now();
    REQUIRE(handshake.start(epoch));

    const auto challenge_request = receive_bounded(
        *server_socket,
        goldsrc::kMaximumConnectionlessChallengeDatagramSize);
    CHECK(challenge_request.payload == *getchallenge.datagram);
    const auto client_endpoint = challenge_request.source;
    REQUIRE(server_socket->send_to(
        client_endpoint,
        challenge_response(challenge),
        error));

    handshake.update(epoch + 1ms);
    const auto connect_request = receive_bounded(
        *server_socket,
        goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect_request.source == client_endpoint);
    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    const auto parsed_connect = goldsrc::parse_connect_request(
        connect_request.payload,
        profile);
    REQUIRE(parsed_connect);
    CHECK(parsed_connect.request->challenge() == challenge);
    REQUIRE(server_socket->send_to(
        client_endpoint,
        accept_response(client_endpoint),
        error));

    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::waiting_for_netchan);
    CHECK(handshake.netchan_session() == nullptr);

    const auto bootstrap_packet = encode_server_packet(
        1U,
        true,
        0U,
        false,
        bytes(kBootstrapPayload));
    REQUIRE(server_socket->send_to(client_endpoint, bootstrap_packet, error));
    handshake.update(epoch + 3ms);
    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::netchan_bootstrap_complete);
    auto* session = handshake.netchan_session();
    REQUIRE(session != nullptr);

    const auto first_acknowledgement = receive_bounded(
        *server_socket,
        goldsrc::kMaximumNetchanDatagramSize);
    CHECK(first_acknowledgement.source == client_endpoint);
    const auto exact_first_ack = bytes(std::array<std::uint8_t, 16U>{
        0x01U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x80U,
        0x5aU, 0x19U, 0x01U, 0x00U,
        0x1aU, 0x01U, 0x11U, 0x40U,
    });
    REQUIRE(first_acknowledgement.payload == exact_first_ack);
    CHECK(session->state().last_outgoing_sequence == sequence(1U));
    CHECK(session->state().next_outgoing_sequence == sequence(2U));
    CHECK(session->state().incoming_sequence == sequence(1U));

    const auto client_reliable = bytes(kClientReliablePayload);
    REQUIRE(session->queue_reliable(client_reliable));
    auto reliable_plan = prepare_outgoing(*session);
    CHECK(reliable_plan.packet().header.sequence.sequence == sequence(2U));
    CHECK(reliable_plan.packet().header.sequence.flags.reliable);
    CHECK(reliable_plan.packet().header.acknowledgement.sequence == sequence(1U));
    CHECK(reliable_plan.packet().header.acknowledgement.reliable);
    auto encoded_reliable = goldsrc::encode_client_to_server_netchan_packet(
        reliable_plan.packet());
    REQUIRE(encoded_reliable);
    REQUIRE(encoded_reliable.datagram);
    const auto sent_reliable = transport.send_to(
        *server_endpoint,
        *encoded_reliable.datagram);
    INFO(sent_reliable.error);
    REQUIRE(sent_reliable);
    commit_outgoing(*session, reliable_plan);

    const auto received_reliable = receive_bounded(
        *server_socket,
        goldsrc::kMaximumNetchanDatagramSize);
    CHECK(received_reliable.source == client_endpoint);
    auto decoded_reliable = goldsrc::decode_client_to_server_netchan_packet(
        received_reliable.payload);
    REQUIRE(decoded_reliable);
    REQUIRE(decoded_reliable.packet);
    CHECK(decoded_reliable.packet->header.sequence.sequence == sequence(2U));
    CHECK(decoded_reliable.packet->header.sequence.flags.reliable);
    CHECK(decoded_reliable.packet->payload == client_reliable);

    const auto server_acknowledgement = encode_server_packet(
        2U,
        false,
        2U,
        true,
        padding_body());
    REQUIRE(server_socket->send_to(
        client_endpoint,
        server_acknowledgement,
        error));
    auto received_acknowledgement = receive_bounded(
        transport,
        goldsrc::kMaximumNetchanDatagramSize);
    const auto processed_acknowledgement = process_server_datagram(
        *session,
        received_acknowledgement.source,
        *server_endpoint,
        received_acknowledgement.payload);
    CHECK(processed_acknowledgement.disposition ==
          DriverDisposition::delivered);
    CHECK_FALSE(processed_acknowledgement.contains_new_reliable_data);
    CHECK_FALSE(session->in_flight_reliable_payload());
    check_no_datagram(*server_socket);

    const auto server_reliable = encode_server_packet(
        3U,
        true,
        2U,
        true,
        bytes(kServerReliablePayload));
    REQUIRE(server_socket->send_to(client_endpoint, server_reliable, error));
    auto received_server_reliable = receive_bounded(
        transport,
        goldsrc::kMaximumNetchanDatagramSize);
    const auto processed_server_reliable = process_server_datagram(
        *session,
        received_server_reliable.source,
        *server_endpoint,
        received_server_reliable.payload);
    REQUIRE(processed_server_reliable.disposition ==
            DriverDisposition::delivered);
    REQUIRE(processed_server_reliable.contains_new_reliable_data);
    REQUIRE(processed_server_reliable.payload);
    CHECK(*processed_server_reliable.payload == bytes(kServerReliablePayload));
    std::size_t reliable_server_deliveries = 1U;
    CHECK_FALSE(session->state().incoming_reliable_acknowledgement);

    auto outgoing_acknowledgement = prepare_outgoing(*session);
    CHECK(outgoing_acknowledgement.packet().header.sequence.sequence == sequence(3U));
    CHECK_FALSE(outgoing_acknowledgement.packet().header.sequence.flags.reliable);
    CHECK(outgoing_acknowledgement.packet().header.acknowledgement.sequence ==
          sequence(3U));
    CHECK_FALSE(outgoing_acknowledgement.packet().header.acknowledgement.reliable);
    auto encoded_outgoing_ack = goldsrc::encode_client_to_server_netchan_packet(
        outgoing_acknowledgement.packet());
    REQUIRE(encoded_outgoing_ack);
    REQUIRE(encoded_outgoing_ack.datagram);
    const auto sent_outgoing_ack = transport.send_to(
        *server_endpoint,
        *encoded_outgoing_ack.datagram);
    INFO(sent_outgoing_ack.error);
    REQUIRE(sent_outgoing_ack);
    commit_outgoing(*session, outgoing_acknowledgement);
    const auto received_outgoing_ack = receive_bounded(
        *server_socket,
        goldsrc::kMaximumNetchanDatagramSize);
    CHECK(received_outgoing_ack.source == client_endpoint);
    auto decoded_outgoing_ack = goldsrc::decode_client_to_server_netchan_packet(
        received_outgoing_ack.payload);
    REQUIRE(decoded_outgoing_ack);
    REQUIRE(decoded_outgoing_ack.packet);
    CHECK(decoded_outgoing_ack.packet->header.acknowledgement.sequence ==
          sequence(3U));
    CHECK_FALSE(decoded_outgoing_ack.packet->header.acknowledgement.reliable);

    const auto before_duplicate = snapshot(*session);
    REQUIRE(server_socket->send_to(client_endpoint, server_reliable, error));
    auto received_duplicate = receive_bounded(
        transport,
        goldsrc::kMaximumNetchanDatagramSize);
    const auto duplicate = process_server_datagram(
        *session,
        received_duplicate.source,
        *server_endpoint,
        received_duplicate.payload);
    CHECK(duplicate.disposition == DriverDisposition::duplicate_ignored);
    CHECK(snapshot(*session) == before_duplicate);
    CHECK(reliable_server_deliveries == 1U);

    const auto old_reliable = encode_server_packet(
        2U,
        true,
        100U,
        false,
        bytes(kServerReliablePayload));
    REQUIRE(server_socket->send_to(client_endpoint, old_reliable, error));
    auto received_old = receive_bounded(
        transport,
        goldsrc::kMaximumNetchanDatagramSize);
    const auto old = process_server_datagram(
        *session,
        received_old.source,
        *server_endpoint,
        received_old.payload);
    CHECK(old.disposition == DriverDisposition::older_ignored);
    CHECK(snapshot(*session) == before_duplicate);
    CHECK(reliable_server_deliveries == 1U);
    check_no_datagram(*server_socket);
}

TEST_CASE(
    "Reliable loss policy is deterministic and remunges canonical bytes",
    "[goldsrc][netchan][reliable][loss][integration]")
{
    goldsrc::NetchanSession session{post_bootstrap_state()};
    const auto canonical = bytes(kClientReliablePayload);
    REQUIRE(session.queue_reliable(canonical));
    auto first = prepare_outgoing(session);
    auto first_encoded = goldsrc::encode_client_to_server_netchan_packet(
        first.packet());
    REQUIRE(first_encoded);
    REQUIRE(first_encoded.datagram);
    commit_outgoing(session, first); // socket send succeeded; fake server drops it

    auto blocked = prepare_outgoing(session);
    CHECK(blocked.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::blocked_waiting_for_ack);
    CHECK_FALSE(blocked.packet().header.sequence.flags.reliable);
    commit_outgoing(session, blocked); // unrelated sequence 3
    REQUIRE(session.in_flight_reliable_payload());
    CHECK_FALSE(session.in_flight_reliable_payload()->retransmission_requested);

    // Capture-confirmed trigger: the old generation ACK advances past latest=2.
    commit_server_header(session, 2U, 3U, false);
    REQUIRE(session.in_flight_reliable_payload());
    REQUIRE(session.in_flight_reliable_payload()->retransmission_requested);

    auto retry = prepare_outgoing(session);
    CHECK(retry.reliable_decision() ==
          goldsrc::ReliableTransmitDecision::retransmit);
    CHECK(retry.packet().header.sequence.sequence == sequence(4U));
    CHECK(retry.packet().header.sequence.flags.reliable);
    auto retry_encoded = goldsrc::encode_client_to_server_netchan_packet(
        retry.packet());
    REQUIRE(retry_encoded);
    REQUIRE(retry_encoded.datagram);

    const auto first_decoded = goldsrc::decode_client_to_server_netchan_packet(
        *first_encoded.datagram);
    const auto retry_decoded = goldsrc::decode_client_to_server_netchan_packet(
        *retry_encoded.datagram);
    REQUIRE(first_decoded);
    REQUIRE(first_decoded.packet);
    REQUIRE(retry_decoded);
    REQUIRE(retry_decoded.packet);
    CHECK(first_decoded.packet->payload == canonical);
    CHECK(retry_decoded.packet->payload == canonical);
    CHECK_FALSE(std::ranges::equal(
        std::span<const std::byte>{*first_encoded.datagram}.subspan(
            goldsrc::kNetchanHeaderSize),
        std::span<const std::byte>{*retry_encoded.datagram}.subspan(
            goldsrc::kNetchanHeaderSize)));

    commit_outgoing(session, retry);
    REQUIRE(session.in_flight_reliable_payload());
    CHECK(session.in_flight_reliable_payload()->bytes == canonical);
    CHECK(session.in_flight_reliable_payload()->toggle);
    CHECK(session.in_flight_reliable_payload()->first_sent_sequence == sequence(2U));
    CHECK(session.in_flight_reliable_payload()->most_recent_sent_sequence ==
          sequence(4U));
    CHECK(session.in_flight_reliable_payload()->send_count == 2U);
    commit_server_header(session, 3U, 4U, true);
    CHECK_FALSE(session.in_flight_reliable_payload());
}

TEST_CASE(
    "Dropped matching ACK and two queued messages preserve reliable lifecycle",
    "[goldsrc][netchan][reliable][loss][toggle][integration]")
{
    SECTION("a later covering matching ACK clears without retransmission")
    {
        goldsrc::NetchanSession session{post_bootstrap_state()};
        const auto canonical = bytes(kClientReliablePayload);
        REQUIRE(session.queue_reliable(canonical));
        auto first = prepare_outgoing(session);
        commit_outgoing(session, first); // reliable sequence 2 was delivered

        // Server sequence 2 / ACK 2 / generation 1 is deliberately dropped.
        auto unrelated = prepare_outgoing(session);
        CHECK(unrelated.reliable_decision() ==
              goldsrc::ReliableTransmitDecision::blocked_waiting_for_ack);
        commit_outgoing(session, unrelated); // client sequence 3
        commit_server_header(session, 3U, 3U, true);
        CHECK_FALSE(session.in_flight_reliable_payload());
        auto next = prepare_outgoing(session);
        CHECK(next.reliable_decision() == goldsrc::ReliableTransmitDecision::none);
        CHECK_FALSE(next.packet().header.sequence.flags.reliable);
    }

    SECTION("A then B alternate internal generation and both finish")
    {
        goldsrc::NetchanSession session{post_bootstrap_state()};
        const auto message_a = bytes("RELIABLE_A");
        const auto message_b = bytes("RELIABLE_B");
        REQUIRE(session.queue_reliable(message_a));
        auto send_a = prepare_outgoing(session);
        commit_outgoing(session, send_a); // sequence 2, generation 1
        REQUIRE(session.in_flight_reliable_payload());
        CHECK(session.in_flight_reliable_payload()->toggle);

        REQUIRE(session.queue_reliable(message_b));
        CHECK(session.pending_reliable_payload() == message_b);
        commit_server_header(session, 2U, 2U, true);
        CHECK_FALSE(session.in_flight_reliable_payload());
        CHECK(session.pending_reliable_payload() == message_b);

        auto send_b = prepare_outgoing(session);
        CHECK(send_b.packet().header.sequence.flags.reliable);
        commit_outgoing(session, send_b); // sequence 3, generation 0
        REQUIRE(session.in_flight_reliable_payload());
        CHECK_FALSE(session.in_flight_reliable_payload()->toggle);
        CHECK(session.in_flight_reliable_payload()->bytes == message_b);
        commit_server_header(session, 3U, 3U, false);
        CHECK_FALSE(session.in_flight_reliable_payload());
        CHECK(session.pending_reliable_payload().empty());
        CHECK_FALSE(session.outgoing_reliable_toggle());
    }
}

TEST_CASE(
    "Persistent driver rejects untrusted input and clears terminal state",
    "[goldsrc][netchan][reliable][security][integration]")
{
    const auto expected = network::NetworkAddress::loopback(27015U);
    const auto rogue = network::NetworkAddress::loopback(27016U);

    SECTION("wrong endpoint malformed body and fragment do not mutate")
    {
        goldsrc::NetchanSession session{post_bootstrap_state()};
        const auto before = snapshot(session);
        const auto valid = encode_server_packet(
            2U,
            true,
            1U,
            false,
            bytes(kServerReliablePayload));
        const auto wrong_endpoint = process_server_datagram(
            session,
            rogue,
            expected,
            valid);
        CHECK(wrong_endpoint.disposition ==
              DriverDisposition::wrong_endpoint_ignored);
        CHECK(snapshot(session) == before);

        const auto malformed = process_server_datagram(
            session,
            expected,
            expected,
            fragmented_server_packet(2U, 1U, false, true));
        CHECK(malformed.disposition == DriverDisposition::malformed);
        CHECK(snapshot(session) == before);

        // Fragment-pending takes precedence over its impossible future ACK.
        const auto fragment = process_server_datagram(
            session,
            expected,
            expected,
            fragmented_server_packet(2U, 3U, false, false));
        CHECK(fragment.disposition ==
              DriverDisposition::fragment_not_processed_by_m232_fixture);
        CHECK(snapshot(session) == before);
    }

    SECTION("send failure abandon and terminal clear preserve atomicity")
    {
        goldsrc::NetchanSession session{post_bootstrap_state()};
        const auto message_a = bytes("RELIABLE_A");
        const auto message_b = bytes("RELIABLE_B");
        REQUIRE(session.queue_reliable(message_a));
        auto failed_send = prepare_outgoing(session);
        const auto encoded = goldsrc::encode_client_to_server_netchan_packet(
            failed_send.packet());
        REQUIRE(encoded);
        const auto queued = snapshot(session);
        REQUIRE(session.abandon_outgoing_packet(std::move(failed_send)));
        CHECK(snapshot(session) == queued);

        auto sent = prepare_outgoing(session);
        commit_outgoing(session, sent);
        REQUIRE(session.queue_reliable(message_b));
        auto outstanding = prepare_outgoing(session);
        const auto sequences_before_clear = snapshot(session);
        session.clear_reliable_state();
        CHECK(session.pending_reliable_payload().empty());
        CHECK_FALSE(session.in_flight_reliable_payload());
        CHECK(session.state().next_outgoing_sequence.value() ==
              sequences_before_clear.next_outgoing);
        CHECK(session.state().last_outgoing_sequence.value() ==
              sequences_before_clear.last_outgoing);
        const auto stale = session.commit_outgoing_send(std::move(outstanding));
        REQUIRE_FALSE(stale);
        REQUIRE(stale.error);
        CHECK(stale.error->code ==
              goldsrc::NetchanSessionErrorCode::stale_outgoing_transaction);
    }
}

} // namespace
