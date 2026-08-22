#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace auth = hlclient::auth;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace network = hlclient::network;
namespace user_fixture = hlclient::test::user_info_fixture;

inline constexpr std::string_view kAuthenticationMarker =
    "TRANSITION_TEST_AUTH";
inline constexpr std::string_view kProtectedAuthentication =
    "TRANSITION_TEST_AUTH_TRANSITION_";
inline constexpr std::array kExactTransitionRequest{
    std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
    std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
    std::byte{0U},
};

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
    return {raw.begin(), raw.end()};
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

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
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
    return {std::move(*prepared.value), std::move(session)};
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
        "Timed out waiting for bounded fake-HLDS transition traffic"};
}

void require_no_datagram(network::UdpSocket& socket)
{
    const auto received = socket.receive(goldsrc::kMaximumNetchanDatagramSize);
    if (received.status == network::ReceiveStatus::received) {
        FAIL("Unexpected post-boundary fake-HLDS transition datagram");
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
    const std::span<const std::byte> payload,
    std::string& error)
{
    REQUIRE(server.send_to(client, payload, error));
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

[[nodiscard]] goldsrc::UserInfoSignonStageConfig user_info_config()
{
    goldsrc::UserInfoSignonStageConfig config;
    auto& driver = config.movement_environment.delta.pre_resource
                       .initial_signon.driver;
    driver.channel_inactivity_timeout = 1s;
    driver.fragment_transfer_timeout = 500ms;
    driver.maximum_datagram_size = 1'100U;
    driver.maximum_fragment_datagram_size = 1'100U;
    driver.maximum_unreliable_payload_size =
        driver.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    driver.maximum_datagrams_per_update = 8U;
    driver.maximum_outgoing_packets_per_update = 8U;
    driver.maximum_events = 64U;
    config.movement_environment.delta.pre_resource.initial_signon.maximum_events =
        64U;
    config.movement_environment.delta.pre_resource.initial_signon
        .maximum_driver_events_per_update = 64U;
    config.movement_environment.delta.pre_resource.maximum_events = 64U;
    config.movement_environment.delta.maximum_events = 64U;
    config.movement_environment.maximum_events = 64U;
    config.maximum_stage_events = 64U;
    return config;
}

[[nodiscard]] goldsrc::ResourceTransitionStageConfig transition_config()
{
    goldsrc::ResourceTransitionStageConfig config;
    config.user_info = user_info_config();
    config.maximum_stage_events = 64U;
    config.maximum_driver_events_per_update = 64U;
    return config;
}

[[nodiscard]] std::vector<std::vector<std::byte>> delta_schemas()
{
    return {
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("bravo_t", delta_fixture::kSchemaBravoFields),
        delta_fixture::schema("charlie_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("delta_t", delta_fixture::kSchemaBravoFields),
        delta_fixture::schema("echo_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("foxtrot_t", delta_fixture::kSchemaBravoFields),
        delta_fixture::schema("golf_t", delta_fixture::kSchemaAlphaFields),
    };
}

[[nodiscard]] std::vector<std::byte> fragmented_batch_user_info_message()
{
    constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string private_info{"\\name\\Synthetic\\model\\scientist"};
    std::uint32_t state = 0xa341'316cU;
    for (std::size_t entry = 0U; entry < 4U; ++entry) {
        private_info += "\\x";
        private_info.push_back(static_cast<char>('0' + entry));
        private_info.push_back('\\');
        for (std::size_t index = 0U; index < 230U; ++index) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            private_info.push_back(alphabet[state % alphabet.size()]);
        }
    }
    REQUIRE(private_info.size() <= goldsrc::kDefaultMaximumUserInfoStringSize);
    return user_fixture::make_message(2U, 0x1234'5678U, private_info);
}

enum class MalformedUserInfoScenario {
    wrong_opcode,
    duplicate_client_index,
    unterminated_info_string,
    oversized_info_string,
    missing_opaque_suffix,
};

[[nodiscard]] std::vector<std::byte> first_semantic_payload_with_user_info(
    const std::span<const std::byte> user_info_messages)
{
    REQUIRE_FALSE(user_info_messages.empty());
    std::vector<std::byte> post_delta;
    move_fixture::append_move_vars_body(post_delta);
    move_fixture::append_confirmed_controls(post_delta);
    post_delta.insert(
        post_delta.end(),
        user_info_messages.begin(),
        user_info_messages.end());
    return delta_fixture::service_payload(
        delta_schemas(),
        goldsrc::kMoveVarsOpcode,
        post_delta);
}

[[nodiscard]] std::vector<std::byte> first_semantic_payload(
    const bool multiple_user_info_messages)
{
    auto user_info_messages = fragmented_batch_user_info_message();
    if (multiple_user_info_messages) {
        const auto second_user_info = user_fixture::make_message(
            3U,
            0x2345'6789U,
            "\\name\\SyntheticTwo\\model\\scientist");
        user_info_messages.insert(
            user_info_messages.end(),
            second_user_info.begin(),
            second_user_info.end());
    }
    return first_semantic_payload_with_user_info(user_info_messages);
}

[[nodiscard]] std::vector<std::byte> malformed_first_semantic_payload(
    const MalformedUserInfoScenario scenario)
{
    std::vector<std::byte> user_info_messages;
    switch (scenario) {
    case MalformedUserInfoScenario::wrong_opcode:
        user_info_messages = fragmented_batch_user_info_message();
        user_info_messages.front() = std::byte{99U};
        break;
    case MalformedUserInfoScenario::duplicate_client_index: {
        user_info_messages = fragmented_batch_user_info_message();
        const auto duplicate = user_fixture::make_message(
            2U,
            0x2345'6789U,
            "\\name\\SecondSynthetic\\model\\scientist");
        user_info_messages.insert(
            user_info_messages.end(),
            duplicate.begin(),
            duplicate.end());
        break;
    }
    case MalformedUserInfoScenario::unterminated_info_string: {
        user_info_messages = fragmented_batch_user_info_message();
        const auto terminator = std::ranges::find(
            user_info_messages.begin() +
                static_cast<std::ptrdiff_t>(user_fixture::kInfoStringOffset),
            user_info_messages.end(),
            std::byte{0U});
        REQUIRE(terminator != user_info_messages.end());
        user_info_messages.erase(terminator);
        break;
    }
    case MalformedUserInfoScenario::oversized_info_string: {
        constexpr std::string_view alphabet =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string private_info{"\\field\\"};
        std::uint32_t state = 0xc801'3ea4U;
        while (private_info.size() <
               goldsrc::kDefaultMaximumUserInfoStringSize + 1U) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            private_info.push_back(alphabet[state % alphabet.size()]);
        }
        REQUIRE(private_info.size() ==
                goldsrc::kDefaultMaximumUserInfoStringSize + 1U);
        user_info_messages = user_fixture::make_message(
            2U,
            0x1234'5678U,
            private_info);
        break;
    }
    case MalformedUserInfoScenario::missing_opaque_suffix: {
        user_info_messages = fragmented_batch_user_info_message();
        const auto terminator = std::ranges::find(
            user_info_messages.begin() +
                static_cast<std::ptrdiff_t>(user_fixture::kInfoStringOffset),
            user_info_messages.end(),
            std::byte{0U});
        REQUIRE(terminator != user_info_messages.end());
        user_info_messages.resize(
            static_cast<std::size_t>(std::distance(
                user_info_messages.begin(),
                terminator)) + 1U);
        break;
    }
    }
    return first_semantic_payload_with_user_info(user_info_messages);
}

[[nodiscard]] std::vector<std::byte> second_semantic_payload()
{
    std::vector<std::byte> output{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{43U},
    };
    std::uint32_t state = 0x243f6a88U;
    for (std::size_t index = 0U; index < 4'096U; ++index) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        output.push_back(static_cast<std::byte>(state & 0xffU));
    }
    return output;
}

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
    std::vector<std::byte> output{
        std::byte{0x42U}, std::byte{0x5aU}, std::byte{0x32U}, std::byte{0U}};
    std::ranges::transform(
        compressed,
        std::back_inserter(output),
        [](const char value) {
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return output;
}

[[nodiscard]] std::vector<std::byte> server_packet(
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

// The fake server authors descriptor bytes independently. Production packet
// encoding is not used to choose the fragment index/count or payload ranges.
[[nodiscard]] std::vector<std::byte> server_fragment(
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
            (reliable_acknowledgement
                 ? goldsrc::kNetchanReliableSequenceFlag
                 : 0U));
    datagram.insert(datagram.end(), body.begin(), body.end());
    return datagram;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket decode_client_packet(
    const network::Datagram& datagram,
    const network::NetworkAddress expected_source)
{
    CHECK(datagram.source == expected_source);
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    return *decoded.packet;
}

[[nodiscard]] bool is_exact_transition_request(
    const goldsrc::ClientToServerNetchanPacket& packet)
{
    return std::ranges::equal(packet.payload, kExactTransitionRequest);
}

void check_transport_only_packet(
    const goldsrc::ClientToServerNetchanPacket& packet)
{
    CHECK_FALSE(packet.header.sequence.flags.fragmented);
    CHECK_FALSE(packet.header.sequence.flags.reliable);
    CHECK(std::ranges::all_of(
        packet.payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
}

struct TraceCounts {
    std::size_t user_info_messages{0U};
    std::size_t first_batch_completions{0U};
    std::size_t requests_queued{0U};
    std::size_t requests_acknowledged{0U};
    std::size_t controls{0U};
    std::size_t boundaries{0U};
};

enum class IntegrationScenario {
    baseline,
    dropped_request,
    dropped_acknowledgement,
    multiple_user_info,
};

struct StartedSession {
    network::NetworkAddress client_endpoint;
    goldsrc::ClientToServerNetchanPacket initial_request;
};

[[nodiscard]] StartedSession reach_first_service_request(
    network::UdpSocket& server,
    goldsrc::GoldSrcHandshakeCoordinator& handshake,
    const goldsrc::ChallengeExchangeTimePoint epoch,
    std::string& error)
{
    REQUIRE(handshake.start(epoch));
    const auto challenge_request = receive_bounded(
        server,
        goldsrc::kMaximumConnectionlessChallengeDatagramSize);
    const auto expected_challenge = goldsrc::build_getchallenge_request();
    REQUIRE(expected_challenge);
    CHECK(challenge_request.payload == *expected_challenge.datagram);
    const auto client_endpoint = challenge_request.source;

    constexpr std::uint32_t challenge = 0x7f00'3111U;
    send_server_datagram(
        server,
        client_endpoint,
        challenge_response(challenge),
        error);
    handshake.update(epoch + 1ms);
    const auto connect = receive_bounded(server, goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect.source == client_endpoint);
    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    const auto parsed = goldsrc::parse_connect_request(connect.payload, profile);
    REQUIRE(parsed);
    REQUIRE(parsed.request);
    CHECK(parsed.request->challenge() == challenge);

    send_server_datagram(
        server,
        client_endpoint,
        accept_response(client_endpoint),
        error);
    handshake.update(epoch + 2ms);
    REQUIRE_FALSE(handshake.terminal());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == client_endpoint);
    CHECK(handshake.connect_send_attempts() == 1U);

    handshake.update(epoch + 3ms);
    const auto initial_datagram = receive_bounded(
        server,
        goldsrc::kMaximumNetchanDatagramSize);
    auto initial = decode_client_packet(initial_datagram, client_endpoint);
    const std::array exact_new{
        std::byte{0x03U}, std::byte{'n'}, std::byte{'e'}, std::byte{'w'},
        std::byte{0U}};
    REQUIRE(initial.payload.size() >= exact_new.size());
    CHECK(std::ranges::equal(
        std::span<const std::byte>{initial.payload}.first(exact_new.size()),
        exact_new));
    CHECK(initial.header.sequence.flags.reliable);
    return {client_endpoint, std::move(initial)};
}

struct FirstServiceBatchExchange {
    std::uint32_t next_server_sequence{1U};
    goldsrc::ChallengeExchangeTimePoint next_update{};
};

[[nodiscard]] FirstServiceBatchExchange send_first_service_batch(
    network::UdpSocket& server,
    goldsrc::GoldSrcHandshakeCoordinator& handshake,
    const StartedSession& started,
    const goldsrc::ChallengeExchangeTimePoint now,
    const std::span<const std::byte> semantic_payload,
    std::string& error)
{
    const auto envelope = service_envelope(semantic_payload);
    const auto fragment_count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(fragment_count_size >= 2U);
    REQUIRE(fragment_count_size <= (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_count = static_cast<std::uint16_t>(fragment_count_size);

    auto client_sequence =
        started.initial_request.header.sequence.sequence.value();
    auto update_time = now;
    std::uint32_t server_sequence = 1U;
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        const auto offset = static_cast<std::size_t>(index - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        send_server_datagram(
            server,
            started.client_endpoint,
            server_fragment(
                server_sequence++,
                client_sequence,
                true,
                index,
                fragment_count,
                std::span<const std::byte>{envelope}.subspan(offset, length)),
            error);
        handshake.update(update_time);
        const auto acknowledgement = decode_client_packet(
            receive_bounded(server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(acknowledgement);
        client_sequence = acknowledgement.header.sequence.sequence.value();
        update_time += 1ms;
    }

    return {server_sequence, update_time};
}

[[nodiscard]] FirstServiceBatchExchange send_first_service_batch(
    network::UdpSocket& server,
    goldsrc::GoldSrcHandshakeCoordinator& handshake,
    const StartedSession& started,
    const goldsrc::ChallengeExchangeTimePoint now,
    const bool multiple_user_info_messages,
    std::string& error)
{
    const auto semantic_payload = first_semantic_payload(
        multiple_user_info_messages);
    return send_first_service_batch(
        server,
        handshake,
        started,
        now,
        semantic_payload,
        error);
}

void run_user_info_stop_point()
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
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::user_info,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        user_info_config(),
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        }};
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} + 7s;
    const auto started = reach_first_service_request(
        *server, handshake, epoch, error);
    CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::waiting_for_user_info);
    (void)send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        false,
        error);

    CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::user_info_complete);
    REQUIRE(handshake.user_info_result());
    CHECK(handshake.user_info_result()->message_count() == 1U);
    CHECK(handshake.user_info_result()->completion().remaining_byte_count() == 0U);
    CHECK(handshake.user_info_result()->source_payload().reassembled());
    CHECK(handshake.user_info_result()->source_payload().decompressed());
    CHECK_FALSE(handshake.resource_transition_result());
    CHECK(traces.user_info_messages == 1U);
    CHECK(traces.first_batch_completions == 1U);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

void run_malformed_user_info(
    const MalformedUserInfoScenario scenario)
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
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::resource_list_boundary,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        },
        transition_config(),
        [&traces](const goldsrc::ResourceTransitionTraceEvent& event) {
            using Classification =
                goldsrc::ResourceTransitionTraceClassification;
            if (event.classification ==
                Classification::transition_request_queued) {
                ++traces.requests_queued;
            }
            if (event.classification ==
                Classification::transition_request_acknowledged) {
                ++traces.requests_acknowledged;
            }
            if (event.classification ==
                Classification::transition_control_decoded) {
                ++traces.controls;
            }
            if (event.classification ==
                Classification::neutral_opcode43_boundary_reached) {
                ++traces.boundaries;
            }
        }};
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{
            9'000 + static_cast<std::int64_t>(scenario)};
    const auto started = reach_first_service_request(
        *server,
        handshake,
        epoch,
        error);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_resource_transition);

    const auto semantic_payload = malformed_first_semantic_payload(scenario);
    const auto first_batch = send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        semantic_payload,
        error);
    for (std::size_t attempt = 0U;
         attempt < 4U && !handshake.terminal();
         ++attempt) {
        handshake.update(
            first_batch.next_update +
            std::chrono::milliseconds{static_cast<std::int64_t>(attempt)});
    }

    // Each fragment acknowledgement was already consumed and checked as
    // transport-only. Anything remaining would be a forbidden transition TX.
    require_no_datagram(*server);
    REQUIRE(handshake.terminal());
    const auto expected_state = scenario == MalformedUserInfoScenario::wrong_opcode
        ? goldsrc::GoldSrcHandshakeState::
              resource_transition_unsupported_message
        : goldsrc::GoldSrcHandshakeState::protocol_error;
    CHECK(handshake.state() == expected_state);
    CHECK_FALSE(handshake.user_info_result());
    CHECK_FALSE(handshake.resource_transition_result());
    REQUIRE(handshake.resource_transition_error());
    CHECK(handshake.resource_transition_error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::user_info_stage_failed);
    REQUIRE(handshake.resource_transition_error()->user_info_code);
    const auto expected_user_info_code =
        scenario == MalformedUserInfoScenario::wrong_opcode
        ? goldsrc::UserInfoSignonStageErrorCode::movement_stage_failed
        : goldsrc::UserInfoSignonStageErrorCode::
              user_info_stream_decode_failed;
    CHECK(*handshake.resource_transition_error()->user_info_code ==
          expected_user_info_code);
    CHECK_FALSE(handshake.error_context().empty());
    CHECK(traces.user_info_messages == 0U);
    CHECK(traces.first_batch_completions == 0U);
    CHECK(traces.requests_queued == 0U);
    CHECK(traces.requests_acknowledged == 0U);
    CHECK(traces.controls == 0U);
    CHECK(traces.boundaries == 0U);
    CHECK(releases == 1U);

    handshake.update(first_batch.next_update + 100ms);
    handshake.cancel(first_batch.next_update + 200ms);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

void run_resource_transition(
    const std::size_t run,
    const IntegrationScenario scenario)
{
    INFO("fake-HLDS resource-transition run " << run + 1U);
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::resource_list_boundary,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        },
        transition_config(),
        [&traces](const goldsrc::ResourceTransitionTraceEvent& event) {
            using Classification =
                goldsrc::ResourceTransitionTraceClassification;
            if (event.classification == Classification::transition_request_queued) {
                ++traces.requests_queued;
            }
            if (event.classification ==
                Classification::transition_request_acknowledged) {
                ++traces.requests_acknowledged;
            }
            if (event.classification == Classification::transition_control_decoded) {
                ++traces.controls;
            }
            if (event.classification ==
                Classification::neutral_opcode43_boundary_reached) {
                ++traces.boundaries;
            }
        }};
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{8'000 + static_cast<std::int64_t>(run)};
    const auto started = reach_first_service_request(
        *server, handshake, epoch, error);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_resource_transition);
    const bool multiple_user_info_messages =
        scenario == IntegrationScenario::multiple_user_info;
    const auto first_batch = send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        multiple_user_info_messages,
        error);
    CHECK_FALSE(handshake.terminal());
    CHECK(traces.first_batch_completions == 1U);

    handshake.update(first_batch.next_update);
    auto transition = decode_client_packet(
        receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
        started.client_endpoint);
    REQUIRE(is_exact_transition_request(transition));
    CHECK(transition.header.sequence.flags.reliable);
    std::size_t transition_datagrams = 1U;
    std::uint32_t last_client_sequence =
        transition.header.sequence.sequence.value();
    std::uint32_t server_sequence = first_batch.next_server_sequence;

    if (scenario == IntegrationScenario::baseline ||
        scenario == IntegrationScenario::multiple_user_info) {
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                false,
                last_client_sequence,
                false,
                {}),
            error);
        handshake.update(first_batch.next_update + 1ms);
        CHECK_FALSE(handshake.terminal());
        CHECK(traces.requests_acknowledged == 1U);
    } else {
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                true,
                last_client_sequence,
                true,
                {}),
            error);
        handshake.update(first_batch.next_update + 1ms);
        auto first_probe_ack = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(first_probe_ack);
        last_client_sequence =
            first_probe_ack.header.sequence.sequence.value();

        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                true,
                last_client_sequence,
                true,
                {}),
            error);
        handshake.update(first_batch.next_update + 2ms);
        auto retry = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        REQUIRE(is_exact_transition_request(retry));
        ++transition_datagrams;
        last_client_sequence = retry.header.sequence.sequence.value();
        CHECK_FALSE(handshake.terminal());
    }

    const auto envelope = service_envelope(second_semantic_payload());
    REQUIRE(envelope.size() > goldsrc::kStockProtocol48NormalFragmentChunkSize);
    const auto count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(count_size >= 3U);
    REQUIRE(count_size <= (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_count = static_cast<std::uint16_t>(count_size);

    auto now = first_batch.next_update + 3ms;
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        const auto offset = static_cast<std::size_t>(index - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        const bool old_generation =
            scenario == IntegrationScenario::dropped_acknowledgement;
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_fragment(
                server_sequence++,
                last_client_sequence,
                old_generation,
                index,
                fragment_count,
                std::span<const std::byte>{envelope}.subspan(offset, length)),
            error);
        handshake.update(now);
        auto response = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        if (is_exact_transition_request(response)) {
            ++transition_datagrams;
        } else {
            check_transport_only_packet(response);
        }
        last_client_sequence = response.header.sequence.sequence.value();
        now += 1ms;
    }

    if (scenario == IntegrationScenario::dropped_acknowledgement) {
        CHECK_FALSE(handshake.terminal());
        CHECK_FALSE(handshake.resource_transition_result());
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence,
                false,
                last_client_sequence,
                false,
                {}),
            error);
        handshake.update(now);
    }

    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::resource_transition_boundary_reached);
    REQUIRE(handshake.terminal());
    REQUIRE(handshake.resource_transition_result());
    const auto& result = *handshake.resource_transition_result();
    const std::size_t expected_user_info_messages =
        multiple_user_info_messages ? 2U : 1U;
    CHECK(result.user_info().message_count() == expected_user_info_messages);
    CHECK(result.user_info().source_payload().reassembled());
    CHECK(result.user_info().source_payload().decompressed());
    CHECK(result.request().message_bytes() == kExactTransitionRequest.size());
    CHECK(result.control().body_bytes() == 8U);
    CHECK(result.boundary().opcode() == 43U);
    CHECK(result.boundary().byte_offset() == 9U);
    CHECK(result.boundary().remaining_byte_count() == 4'096U);
    CHECK(result.source_payload().reassembled());
    CHECK(result.source_payload().decompressed());
    CHECK(traces.user_info_messages == expected_user_info_messages);
    CHECK(traces.first_batch_completions == 1U);
    CHECK(traces.requests_queued == 1U);
    CHECK(traces.requests_acknowledged == 1U);
    CHECK(traces.controls == 1U);
    CHECK(traces.boundaries == 1U);
    if (scenario == IntegrationScenario::baseline ||
        scenario == IntegrationScenario::multiple_user_info) {
        CHECK(transition_datagrams == 1U);
    } else if (scenario == IntegrationScenario::dropped_request) {
        CHECK(transition_datagrams == 2U);
    } else {
        CHECK(transition_datagrams >= 2U);
    }
    CHECK(handshake.error_context().empty());
    CHECK(releases == 1U);
    require_no_datagram(*server);
    handshake.update(now + 100ms);
    handshake.cancel(now + 200ms);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

} // namespace

TEST_CASE("Fake HLDS user-info stop point sends no transition request",
          "[goldsrc][userinfo][udp][stop-point][security][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("fake-HLDS user-info stop run " << run + 1U);
        run_user_info_stop_point();
    }
}

TEST_CASE("Fake HLDS malformed inbound user-info fails atomically before sendres",
          "[goldsrc][userinfo][resource-transition][udp][negative][malformed-userinfo]")
{
    SECTION("wrong opcode at the exact opcode-13 boundary") {
        run_malformed_user_info(MalformedUserInfoScenario::wrong_opcode);
    }
    SECTION("duplicate client index") {
        run_malformed_user_info(
            MalformedUserInfoScenario::duplicate_client_index);
    }
    SECTION("unterminated info string") {
        run_malformed_user_info(
            MalformedUserInfoScenario::unterminated_info_string);
    }
    SECTION("info string exceeds the default project limit") {
        run_malformed_user_info(
            MalformedUserInfoScenario::oversized_info_string);
    }
    SECTION("fixed opaque suffix is missing") {
        run_malformed_user_info(
            MalformedUserInfoScenario::missing_opaque_suffix);
    }
}

TEST_CASE("Fake HLDS full resource-transition boundary repeats 20 of 20",
          "[goldsrc][resource-transition][udp][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(run, IntegrationScenario::baseline);
    }
}

TEST_CASE("Fake HLDS dropped transition request repeats 20 of 20 without semantic requeue",
          "[goldsrc][resource-transition][udp][drop-request][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(run, IntegrationScenario::dropped_request);
    }
}

TEST_CASE("Fake HLDS dropped transition ACK repeats 20 of 20 with bounded pre-ACK payload",
          "[goldsrc][resource-transition][udp][drop-ack][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(
            run,
            IntegrationScenario::dropped_acknowledgement);
    }
}

TEST_CASE("Fake HLDS multi-userinfo differential repeats 20 of 20",
          "[goldsrc][resource-transition][udp][multi-userinfo][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(run, IntegrationScenario::multiple_user_info);
    }
}
