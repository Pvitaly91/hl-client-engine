#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/network/datagram_transport.hpp>
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
namespace auth = hlclient::auth;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

inline constexpr std::string_view kAuthenticationMarker =
    "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
inline constexpr std::string_view kStockLengthEarlyText =
    "0123456789012345678901234567890123456789";

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
    output.push_back(static_cast<std::byte>(value & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_u32_le(std::vector<std::byte>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::byte>(value & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void append_nul_string(
    std::vector<std::byte>& output,
    const std::string_view value)
{
    const auto raw = std::as_bytes(std::span{value.data(), value.size()});
    output.insert(output.end(), raw.begin(), raw.end());
    output.push_back(std::byte{0U});
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

struct PreparedWithSession {
    goldsrc::PreparedConnectRequest request;
    auth::AuthenticationSession session;
};

[[nodiscard]] PreparedWithSession prepared_request_with_session(
    std::size_t& releases)
{
    std::vector<std::byte> suffix(
        goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = bytes(kAuthenticationMarker);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto material = goldsrc::AuthenticationMaterial::create(
        bytes(kProtectedAuthentication),
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
    throw std::runtime_error{
        "Timed out waiting for bounded fake-HLDS pre-resource traffic"};
}

void require_no_datagram(network::UdpSocket& socket)
{
    const auto received = socket.receive(goldsrc::kMaximumNetchanDatagramSize);
    if (received.status == network::ReceiveStatus::received) {
        FAIL("Unexpected fake-HLDS datagram after the pre-resource boundary");
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

[[nodiscard]] std::vector<std::byte> semantic_payload(
    const bool include_leading_text,
    const std::uint8_t maximum_clients,
    const std::string_view map_path,
    const std::size_t boundary_body_size)
{
    REQUIRE(boundary_body_size > 0U);
    std::vector<std::byte> output;
    if (include_leading_text) {
        output.push_back(std::byte{8U});
        append_nul_string(output, kStockLengthEarlyText);
    }
    output.push_back(std::byte{11U});
    append_u32_le(output, 48U);
    append_u32_le(output, 0x1234'5678U);
    append_u32_le(output, 0xdead'beefU);
    for (std::uint8_t value = 0U; value < 16U; ++value) {
        output.push_back(static_cast<std::byte>(value));
    }
    output.push_back(static_cast<std::byte>(maximum_clients));
    output.push_back(std::byte{0U});
    output.push_back(maximum_clients == 1U ? std::byte{0U} : std::byte{1U});
    append_nul_string(output, "valve");
    append_nul_string(output, "Fake HLDS");
    append_nul_string(output, map_path);
    append_nul_string(output, "boot_camp crossfire");
    output.push_back(std::byte{0U});
    output.push_back(
        static_cast<std::byte>(goldsrc::kPreResourceSimpleControlOpcode));
    output.push_back(std::byte{0U});
    output.push_back(std::byte{0U});
    output.push_back(
        static_cast<std::byte>(goldsrc::kPreResourceComplexBoundaryOpcode));
    std::uint32_t state = 0x6d2b'79f5U;
    for (std::size_t index = 0U; index < boundary_body_size; ++index) {
        state = state * 1'664'525U + 1'013'904'223U;
        output.push_back(static_cast<std::byte>(state >> 24U));
    }
    return output;
}

// Independent standard-BZip2 construction: production envelope and service
// decoders are not used to construct fake-server traffic.
[[nodiscard]] std::vector<std::byte> service_envelope(
    const std::span<const std::byte> semantic)
{
    REQUIRE_FALSE(semantic.empty());
    REQUIRE(semantic.size() <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> source;
    source.reserve(semantic.size());
    std::ranges::transform(
        semantic,
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

    auto output = bytes({0x42U, 0x5aU, 0x32U, 0x00U});
    std::ranges::transform(
        compressed,
        std::back_inserter(output),
        [](const char value) {
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return output;
}

[[nodiscard]] std::vector<std::byte> encode_server_packet(
    const std::uint32_t acknowledgement,
    std::vector<std::byte> payload)
{
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(1U),
                goldsrc::NetchanSequenceFlags{true, false},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                true,
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

// Independent normal-fragment descriptor construction for fake-HLDS traffic.
[[nodiscard]] std::vector<std::byte> independent_server_fragment(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
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
        acknowledgement | goldsrc::kNetchanReliableSequenceFlag);
    datagram.insert(datagram.end(), body.begin(), body.end());
    return datagram;
}

[[nodiscard]] goldsrc::ChallengeExchangeConfig challenge_config()
{
    goldsrc::ChallengeExchangeConfig config;
    config.retry_interval = 100ms;
    config.timeout = 1s;
    config.maximum_attempts = 2U;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] goldsrc::ConnectResponseWaitConfig response_config()
{
    goldsrc::ConnectResponseWaitConfig config;
    config.timeout = 1s;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] goldsrc::PreResourceSignonConfig integration_config()
{
    goldsrc::PreResourceSignonConfig config;
    config.initial_signon.driver.channel_inactivity_timeout = 1s;
    config.initial_signon.driver.fragment_transfer_timeout = 500ms;
    config.initial_signon.driver.maximum_datagram_size = 1'100U;
    config.initial_signon.driver.maximum_fragment_datagram_size = 1'100U;
    config.initial_signon.driver.maximum_unreliable_payload_size =
        config.initial_signon.driver.maximum_datagram_size -
        goldsrc::kNetchanHeaderSize;
    config.initial_signon.driver.maximum_datagrams_per_update = 8U;
    config.initial_signon.driver.maximum_outgoing_packets_per_update = 8U;
    config.initial_signon.driver.maximum_events = 32U;
    config.initial_signon.maximum_events = 32U;
    config.initial_signon.maximum_driver_events_per_update = 32U;
    config.maximum_events = 32U;
    return config;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket require_initial_request(
    const network::Datagram& datagram,
    const network::NetworkAddress client_endpoint)
{
    CHECK(datagram.source == client_endpoint);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.sequence == sequence(1U));
    CHECK(decoded.packet->header.sequence.flags.reliable);
    CHECK_FALSE(decoded.packet->header.sequence.flags.fragmented);
    REQUIRE(decoded.packet->payload.size() == 8U);
    const auto exact = bytes({0x03U, 0x6eU, 0x65U, 0x77U, 0x00U});
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

void require_transport_ack(
    network::UdpSocket& server,
    const network::NetworkAddress client_endpoint)
{
    const auto datagram = receive_bounded(
        server,
        goldsrc::kMaximumNetchanDatagramSize);
    CHECK(datagram.source == client_endpoint);
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK_FALSE(decoded.packet->header.sequence.flags.reliable);
    CHECK(std::ranges::all_of(
        decoded.packet->payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
}

struct TraceCounts {
    std::size_t server_info{0U};
    std::size_t control{0U};
    std::size_t boundary{0U};
};

enum class Scenario {
    baseline,
    fragmented_reordered,
    differential,
};

void run_fake_hlds(const std::size_t run, const Scenario scenario)
{
    INFO("fake-HLDS pre-resource run " << run + 1U << "/20");
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

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::pre_resource,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {},
        {},
        {},
        {},
        integration_config(),
        [&traces](const goldsrc::PreResourceSignonTraceEvent& event) {
            using Classification =
                goldsrc::PreResourceSignonTraceClassification;
            switch (event.classification) {
            case Classification::server_info_ready:
                ++traces.server_info;
                break;
            case Classification::pre_resource_control:
                ++traces.control;
                break;
            case Classification::pre_resource_boundary_reached:
                ++traces.boundary;
                break;
            case Classification::stage_started:
            case Classification::initial_boundary_reached:
            case Classification::stage_cancelled:
            case Classification::stage_timed_out:
            case Classification::secondary_stream_pending_m3:
            case Classification::unsupported_message:
            case Classification::backpressure:
            case Classification::network_failure:
            case Classification::protocol_failure:
                break;
            }
        }};

    const auto scenario_offset = static_cast<std::size_t>(scenario) * 100U;
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{static_cast<std::int64_t>(
            scenario_offset + run + 1U)};
    REQUIRE(handshake.start(epoch));
    const auto challenge_request = receive_bounded(
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
    const auto connect = receive_bounded(
        *server,
        goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect.source == client_endpoint);
    const auto parsed = goldsrc::parse_connect_request(connect.payload, [] {
        auto profile = goldsrc::ConnectCompatibilityProfile{};
        profile.protected_authentication_is_ascii_hex = false;
        return profile;
    }());
    REQUIRE(parsed);
    CHECK(parsed.request->challenge() == challenge);

    send_server_datagram(
        *server,
        client_endpoint,
        accept_response(client_endpoint),
        error);
    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::waiting_for_pre_resource);
    CHECK_FALSE(handshake.terminal());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == client_endpoint);
    REQUIRE(handshake.connect_response());
    CHECK(std::holds_alternative<goldsrc::ConnectAccepted>(
        *handshake.connect_response()));
    CHECK(handshake.connect_send_attempts() == 1U);
    CHECK_FALSE(handshake.netchan_bootstrap_result());
    CHECK_FALSE(handshake.initial_signon_result());
    CHECK_FALSE(handshake.pre_resource_result());
    CHECK(releases == 0U);
    require_no_datagram(*server);

    handshake.update(epoch + 3ms);
    const auto request_datagram = receive_bounded(
        *server,
        goldsrc::kMaximumNetchanDatagramSize);
    const auto request = require_initial_request(
        request_datagram,
        client_endpoint);
    CHECK(request.header.acknowledgement.sequence == sequence(0U));
    CHECK_FALSE(request.header.acknowledgement.reliable);
    CHECK(releases == 0U);

    const bool differential = scenario == Scenario::differential;
    const bool include_leading_text = !differential || (run % 2U != 0U);
    const std::uint8_t maximum_clients =
        differential && !include_leading_text ? 1U :
        differential ? 4U : 8U;
    const std::string_view map_path =
        differential && run % 3U == 0U ? "maps/a.bsp" :
                                         "maps/boot_camp_extended.bsp";
    const std::size_t boundary_body_size =
        scenario == Scenario::fragmented_reordered ? 3'500U : 2U;
    const auto semantic = semantic_payload(
        include_leading_text,
        maximum_clients,
        map_path,
        boundary_body_size);
    const auto envelope = service_envelope(semantic);

    if (scenario != Scenario::fragmented_reordered) {
        REQUIRE(envelope.size() <=
                goldsrc::kStockProtocol48NormalFragmentChunkSize);
        send_server_datagram(
            *server,
            client_endpoint,
            encode_server_packet(
                request.header.sequence.sequence.value(),
                envelope),
            error);
        handshake.update(epoch + 4ms);
        require_transport_ack(*server, client_endpoint);
    } else {
        REQUIRE(envelope.size() >
                goldsrc::kStockProtocol48NormalFragmentChunkSize);
        const auto count_size =
            (envelope.size() +
             goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        REQUIRE(count_size >= 3U);
        REQUIRE(count_size <= (std::numeric_limits<std::uint16_t>::max)());
        const auto count = static_cast<std::uint16_t>(count_size);
        const auto final_offset = static_cast<std::size_t>(count - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto final_datagram = independent_server_fragment(
            1U,
            request.header.sequence.sequence.value(),
            count,
            count,
            std::span<const std::byte>{envelope}.subspan(final_offset));

        // Final-first plus an exact same-sequence duplicate exercises bounded
        // out-of-order reassembly without duplicate publication or ACK.
        send_server_datagram(
            *server,
            client_endpoint,
            final_datagram,
            error);
        handshake.update(epoch + 4ms);
        require_transport_ack(*server, client_endpoint);
        send_server_datagram(
            *server,
            client_endpoint,
            final_datagram,
            error);
        handshake.update(epoch + 5ms);
        require_no_datagram(*server);

        auto now = epoch + 6ms;
        std::uint32_t packet_sequence = 2U;
        for (std::uint16_t index = 1U; index < count; ++index) {
            const auto offset = static_cast<std::size_t>(index - 1U) *
                goldsrc::kStockProtocol48NormalFragmentChunkSize;
            const auto length = (std::min)(
                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                envelope.size() - offset);
            send_server_datagram(
                *server,
                client_endpoint,
                independent_server_fragment(
                    packet_sequence,
                    request.header.sequence.sequence.value(),
                    index,
                    count,
                    std::span<const std::byte>{envelope}.subspan(offset, length)),
                error);
            handshake.update(now);
            require_transport_ack(*server, client_endpoint);
            ++packet_sequence;
            now += 1ms;
        }
    }

    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::pre_resource_boundary_reached);
    REQUIRE(handshake.terminal());
    CHECK_FALSE(handshake.initial_signon_result());
    REQUIRE(handshake.pre_resource_result());
    const auto& result = *handshake.pre_resource_result();
    CHECK(result.server_info().protocol_version() ==
          goldsrc::ProtocolVersion::goldsrc_48);
    CHECK(result.server_info().maximum_clients().value() == maximum_clients);
    CHECK(result.server_info().multi_client_mode() ==
          (maximum_clients != 1U));
    CHECK(result.server_info().game_directory() == "valve");
    CHECK(result.server_info().map_file_path() == map_path);
    REQUIRE(result.controls().size() == 1U);
    CHECK(result.controls().front().opcode() ==
          goldsrc::kPreResourceSimpleControlOpcode);
    CHECK(result.boundary().opcode() ==
          goldsrc::kPreResourceComplexBoundaryOpcode);
    CHECK(result.boundary().remaining_byte_count() == boundary_body_size);
    CHECK(result.boundary().direction() ==
          goldsrc::ResourcePhaseBoundaryDirection::server_message);
    CHECK(result.boundary().evidence_status() ==
          goldsrc::ResourcePhaseEvidenceStatus::
              confirmed_pre_resource_boundary_body_pending);
    CHECK(result.source_payload().payload_size() == semantic.size());
    CHECK(result.source_payload().initial_boundary_offset() ==
          (include_leading_text ? 42U : 0U));
    CHECK(result.source_payload().server_info_body_offset() ==
          (include_leading_text ? 43U : 1U));
    const std::size_t expected_server_info_body_size =
        36U + std::string_view{"valve"}.size() +
        std::string_view{"Fake HLDS"}.size() + map_path.size() +
        std::string_view{"boot_camp crossfire"}.size();
    CHECK(result.source_payload().server_info_body_size() ==
          expected_server_info_body_size);
    REQUIRE(result.controls().size() == 1U);
    CHECK(result.controls().front().byte_offset() ==
          result.source_payload().server_info_body_offset() +
              expected_server_info_body_size);
    CHECK(result.boundary().byte_offset() ==
          result.controls().front().byte_offset() + 3U);
    CHECK(result.source_payload().reassembled() ==
          (scenario == Scenario::fragmented_reordered));
    CHECK(result.source_payload().decompressed());
    CHECK(result.source_payload().direction() ==
          goldsrc::NetchanDirection::server_to_client);
    CHECK(traces.server_info == 1U);
    CHECK(traces.control == 1U);
    CHECK(traces.boundary == 1U);
    CHECK(handshake.error_context().empty());
    CHECK(releases == 1U);
    require_no_datagram(*server);

    handshake.update(epoch + 1s);
    handshake.cancel(epoch + 2s);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::pre_resource_boundary_reached);
    CHECK(traces.server_info == 1U);
    CHECK(traces.boundary == 1U);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

} // namespace

TEST_CASE("Fake HLDS pre-resource baseline repeats 20 of 20",
          "[goldsrc][pre-resource][udp][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_fake_hlds(run, Scenario::baseline);
    }
}

TEST_CASE("Fake HLDS pre-resource fragmented reordered duplicate repeats 20 of 20",
          "[goldsrc][pre-resource][udp][fragment][reordered][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_fake_hlds(run, Scenario::fragmented_reordered);
    }
}

TEST_CASE("Fake HLDS pre-resource differential offsets repeat 20 of 20",
          "[goldsrc][pre-resource][udp][differential][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_fake_hlds(run, Scenario::differential);
    }
}
