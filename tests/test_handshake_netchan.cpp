#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

using goldsrc::ChallengeExchangeTimePoint;
using goldsrc::GoldSrcHandshakeCoordinator;
using goldsrc::GoldSrcHandshakeState;
using network::Datagram;
using network::DatagramLocalAddressResult;
using network::DatagramSendResult;
using network::DatagramSendStatus;
using network::DatagramTransportReceiveResult;
using network::DatagramTransportReceiveStatus;
using network::IDatagramTransport;
using network::NetworkAddress;

inline constexpr std::string_view kSyntheticAuthenticationMarker =
    "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";

struct SentDatagram {
    NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeTransport final : public IDatagramTransport {
public:
    [[nodiscard]] DatagramLocalAddressResult local_address() const override
    {
        ++local_address_calls;
        return DatagramLocalAddressResult{local, {}};
    }

    [[nodiscard]] DatagramSendResult send_to(
        const NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        sent.push_back(SentDatagram{
            destination,
            std::vector<std::byte>{payload.begin(), payload.end()},
        });
        if (failing_send_call && sent.size() == *failing_send_call) {
            return DatagramSendResult{
                DatagramSendStatus::error,
                "synthetic send failure",
            };
        }
        return DatagramSendResult{DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] DatagramTransportReceiveResult receive(std::size_t) override
    {
        ++receive_calls;
        if (incoming.empty()) {
            return DatagramTransportReceiveResult{
                DatagramTransportReceiveStatus::would_block,
                std::nullopt,
                std::nullopt,
                0U,
                {},
            };
        }
        auto result = std::move(incoming.front());
        incoming.pop_front();
        return result;
    }

    void queue(const NetworkAddress source, std::vector<std::byte> payload)
    {
        const auto size = payload.size();
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            Datagram{source, std::move(payload)},
            source,
            size,
            {},
        });
    }

    void queue_receive_error()
    {
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic receive failure",
        });
    }

    NetworkAddress local{NetworkAddress::loopback(30'000)};
    std::optional<std::size_t> failing_send_call;
    mutable std::size_t local_address_calls{0U};
    std::size_t receive_calls{0U};
    std::vector<SentDatagram> sent;
    std::deque<DatagramTransportReceiveResult> incoming;
};

class CountingLifetime final : public hlclient::auth::IAuthenticationSessionLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept : releases_{releases} {}
    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

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

[[nodiscard]] std::vector<std::byte> challenge_response(
    const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 " + std::to_string(challenge) +
              " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> accepted_response()
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "B 1 \"127.0.0.1:30000\" 0 10210";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> rejected_response()
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "9Invalid connection.\n";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result.has_value());
    return *result;
}

[[nodiscard]] goldsrc::NetchanHeader server_header(
    const std::uint32_t sequence_value,
    const bool reliable,
    const bool fragmented,
    const std::uint32_t acknowledgement_value,
    const bool reliable_acknowledgement)
{
    return goldsrc::NetchanHeader{
        goldsrc::NetchanSequenceWord{
            sequence(sequence_value),
            goldsrc::NetchanSequenceFlags{reliable, fragmented},
        },
        goldsrc::NetchanAcknowledgementWord{
            sequence(acknowledgement_value),
            reliable_acknowledgement,
        },
    };
}

[[nodiscard]] std::vector<std::byte> encode_server_packet(
    goldsrc::ServerToClientNetchanPacket packet)
{
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram.has_value());
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> unfragmented_server_packet(
    const std::span<const std::byte> payload)
{
    return encode_server_packet(goldsrc::ServerToClientNetchanPacket{
        server_header(1U, true, false, 0U, false),
        {},
        std::vector<std::byte>{payload.begin(), payload.end()},
    });
}

[[nodiscard]] std::vector<std::byte> fragmented_server_packet()
{
    auto datagram = bytes(std::array<std::uint8_t, 22U>{
        0x01U, 0x00U, 0x00U, 0xc0U, // reliable + fragment, sequence 1
        0x00U, 0x00U, 0x00U, 0x00U, // acknowledgement 0
        0x01U,                         // slot 0 present
        0x01U, 0x00U, 0x01U, 0x00U, // fragment id 0x00010001
        0x00U, 0x00U,                // offset 0
        0x04U, 0x00U,                // four-byte fragment
        0x00U,                         // slot 1 absent
        0x90U, 0x91U, 0x92U, 0x93U, // opaque fragment bytes
    });
    goldsrc::encode_netchan_payload(
        std::span<std::byte>{datagram}.subspan(goldsrc::kNetchanHeaderSize),
        sequence(1U));
    return datagram;
}

[[nodiscard]] std::vector<std::byte> normal_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    std::vector<std::byte> fragment)
{
    const auto fragment_size = fragment.size();
    goldsrc::NetchanFragmentSlots slots;
    slots[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        (static_cast<std::uint32_t>(fragment_index) << 16U) |
            static_cast<std::uint32_t>(fragment_count),
        0U,
        static_cast<std::uint16_t>(fragment_size),
        0U,
    };
    return encode_server_packet(goldsrc::ServerToClientNetchanPacket{
        server_header(packet_sequence, true, true, 0U, false),
        std::move(slots),
        std::move(fragment),
        fragment_size,
    });
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket decode_client_packet(
    const SentDatagram& sent)
{
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(sent.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet.has_value());
    return *decoded.packet;
}

struct PreparedWithSession {
    goldsrc::PreparedConnectRequest request;
    hlclient::auth::AuthenticationSession session;
};

[[nodiscard]] PreparedWithSession prepare_with_session(std::size_t& releases)
{
    std::vector<std::byte> suffix(goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = bytes(kSyntheticAuthenticationMarker);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto material = goldsrc::AuthenticationMaterial::create(
        bytes(kSyntheticProtectedAuthentication),
        suffix);
    REQUIRE(material);

    hlclient::auth::AuthenticationSession session{
        std::move(*material.value),
        std::make_unique<CountingLifetime>(releases),
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

[[nodiscard]] goldsrc::ChallengeExchangeConfig challenge_config()
{
    goldsrc::ChallengeExchangeConfig config;
    config.retry_interval = 100ms;
    config.timeout = 350ms;
    config.maximum_attempts = 2U;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] goldsrc::ConnectResponseWaitConfig response_config()
{
    goldsrc::ConnectResponseWaitConfig config;
    config.timeout = 50ms;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] goldsrc::NetchanBootstrapConfig netchan_config()
{
    goldsrc::NetchanBootstrapConfig config;
    config.first_packet_timeout = 50ms;
    config.maximum_datagrams_per_update = 4U;
    config.maximum_outgoing_packets_per_update = 1U;
    return config;
}

[[nodiscard]] GoldSrcHandshakeCoordinator coordinator(
    FakeTransport& transport,
    const NetworkAddress remote,
    PreparedWithSession prepared,
    const goldsrc::HandshakeStopPoint stop_point =
        goldsrc::HandshakeStopPoint::netchan_bootstrap,
    goldsrc::NetchanBootstrapConfig bootstrap_config = netchan_config())
{
    return GoldSrcHandshakeCoordinator{
        transport,
        remote,
        stop_point,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        std::move(bootstrap_config),
        {},
    };
}

void reach_response_wait(
    FakeTransport& transport,
    GoldSrcHandshakeCoordinator& handshake,
    const NetworkAddress remote,
    const ChallengeExchangeTimePoint epoch,
    const std::size_t& releases)
{
    REQUIRE(handshake.start(epoch));
    REQUIRE(transport.sent.size() == 1U);
    REQUIRE(transport.sent.front().destination == remote);
    CHECK(releases == 0U);

    transport.queue(remote, challenge_response(123'456U));
    handshake.update(epoch + 1ms);
    REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_connect_response);
    REQUIRE_FALSE(handshake.terminal());
    REQUIRE(transport.sent.size() == 2U);
    CHECK(transport.sent.back().destination == remote);
    CHECK(handshake.connect_send_attempts() == 1U);
    REQUIRE(handshake.local_endpoint().has_value());
    CHECK(*handshake.local_endpoint() == transport.local);
    CHECK(releases == 0U);
}

void reach_netchan_wait(
    FakeTransport& transport,
    GoldSrcHandshakeCoordinator& handshake,
    const NetworkAddress remote,
    const ChallengeExchangeTimePoint epoch,
    const std::size_t& releases)
{
    reach_response_wait(transport, handshake, remote, epoch, releases);
    transport.queue(remote, accepted_response());
    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_netchan);
    REQUIRE_FALSE(handshake.terminal());
    REQUIRE(transport.sent.size() == 2U);
    CHECK(handshake.connect_send_attempts() == 1U);
    REQUIRE(handshake.local_endpoint().has_value());
    CHECK(*handshake.local_endpoint() == transport.local);
    CHECK(releases == 0U);
}

void check_terminal_idempotence(
    FakeTransport& transport,
    GoldSrcHandshakeCoordinator& handshake,
    const ChallengeExchangeTimePoint now,
    const std::size_t& releases)
{
    REQUIRE(handshake.terminal());
    const auto state = handshake.state();
    const auto send_count = transport.sent.size();
    const auto receive_count = transport.receive_calls;
    const auto queued_count = transport.incoming.size();

    handshake.update(now + 1s);
    handshake.cancel(now + 2s);
    handshake.update(now + 3s);

    CHECK(handshake.state() == state);
    CHECK(transport.sent.size() == send_count);
    CHECK(transport.receive_calls == receive_count);
    CHECK(transport.incoming.size() == queued_count);
    CHECK(releases == 1U);
}

TEST_CASE("M2.3.1 coordinator hands ACCEPT to netchan on the same socket without same-call RX",
          "[goldsrc][handshake][netchan][coordinator]")
{
    FakeTransport transport;
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    std::size_t releases = 0U;
    auto handshake = coordinator(transport, remote, prepare_with_session(releases));

    reach_response_wait(transport, handshake, remote, epoch, releases);
    const auto opaque_payload = bytes(std::array<std::uint8_t, 9U>{
        0xffU,
        0x00U,
        0xfeU,
        0x81U,
        0x53U,
        0x56U,
        0x43U,
        0x00U,
        0xa5U,
    });
    transport.queue(remote, accepted_response());
    transport.queue(remote, unfragmented_server_packet(opaque_payload));
    const auto receives_before_accept = transport.receive_calls;

    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_netchan);
    REQUIRE_FALSE(handshake.terminal());
    REQUIRE(transport.sent.size() == 2U);
    CHECK(transport.receive_calls == receives_before_accept + 1U);
    REQUIRE(transport.incoming.size() == 1U);
    CHECK(releases == 0U);
    CHECK(handshake.connect_send_attempts() == 1U);
    REQUIRE(handshake.local_endpoint().has_value());
    CHECK(*handshake.local_endpoint() == transport.local);

    handshake.update(epoch + 3ms);
    REQUIRE(handshake.state() == GoldSrcHandshakeState::netchan_bootstrap_complete);
    REQUIRE(handshake.terminal());
    REQUIRE(transport.sent.size() == 3U);
    CHECK(transport.incoming.empty());
    CHECK(releases == 1U);

    const auto acknowledgement = decode_client_packet(transport.sent[2U]);
    CHECK(acknowledgement.header.sequence.sequence.value() == 1U);
    CHECK_FALSE(acknowledgement.header.sequence.flags.reliable);
    CHECK_FALSE(acknowledgement.header.sequence.flags.fragmented);
    CHECK(acknowledgement.header.acknowledgement.sequence.value() == 1U);
    CHECK(acknowledgement.header.acknowledgement.reliable);
    REQUIRE(
        acknowledgement.payload.size() ==
        goldsrc::kStockProtocol48MinimumDecodedPayloadSize);
    CHECK(std::ranges::all_of(
        acknowledgement.payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));

    REQUIRE(handshake.netchan_bootstrap_result().has_value());
    const auto& result = handshake.netchan_bootstrap_result()->payload;
    CHECK(result.bytes == opaque_payload);
    CHECK(result.source_sequence.value() == 1U);
    CHECK(result.source_acknowledgement.value() == 0U);
    CHECK(result.sequence_flags.reliable);
    CHECK_FALSE(result.sequence_flags.fragmented);
    CHECK_FALSE(result.acknowledgement_reliable);
    CHECK(result.direction == goldsrc::NetchanDirection::server_to_client);
    CHECK(result.received_at == epoch + 3ms);

    check_terminal_idempotence(transport, handshake, epoch + 3ms, releases);
    CHECK(handshake.netchan_bootstrap_result()->payload.bytes == opaque_payload);
}

TEST_CASE("M2.3.1 authentication lifetime spans ACCEPT and every netchan terminal path",
          "[goldsrc][handshake][netchan][auth][lifetime]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("rejection releases before any driver handoff")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_response_wait(transport, handshake, remote, epoch, releases);
        transport.queue(remote, rejected_response());

        handshake.update(epoch + 2ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::rejected);
        CHECK(releases == 1U);
        CHECK_FALSE(handshake.netchan_bootstrap_result());
        check_terminal_idempotence(transport, handshake, epoch + 2ms, releases);
    }

    SECTION("coordinator destruction releases a handed-off active driver guard")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        {
            auto handshake = coordinator(
                transport,
                remote,
                prepare_with_session(releases));
            reach_netchan_wait(transport, handshake, remote, epoch, releases);
            CHECK(releases == 0U);
        }
        CHECK(releases == 1U);
    }

    SECTION("first packet timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_netchan_wait(transport, handshake, remote, epoch, releases);

        handshake.update(epoch + 52ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::netchan_timed_out);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 52ms, releases);
    }

    SECTION("invalid driver start releases the handed-off guard")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto invalid_config = netchan_config();
        invalid_config.maximum_events = 0U;
        auto handshake = coordinator(
            transport,
            remote,
            prepare_with_session(releases),
            goldsrc::HandshakeStopPoint::netchan_bootstrap,
            invalid_config);
        reach_response_wait(transport, handshake, remote, epoch, releases);
        transport.queue(remote, accepted_response());

        handshake.update(epoch + 2ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::configuration_error);
        CHECK(handshake.terminal());
        CHECK(releases == 1U);
        CHECK(transport.sent.size() == 2U);
        check_terminal_idempotence(transport, handshake, epoch + 2ms, releases);
    }

    SECTION("incomplete normal transfer uses its fixed fragment deadline")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto config = netchan_config();
        config.fragment_transfer_timeout = 20ms;
        auto handshake = coordinator(
            transport,
            remote,
            prepare_with_session(releases),
            goldsrc::HandshakeStopPoint::netchan_bootstrap,
            config);
        reach_netchan_wait(transport, handshake, remote, epoch, releases);
        transport.queue(
            remote,
            normal_fragment_packet(
                1U,
                1U,
                2U,
                std::vector<std::byte>(
                    goldsrc::kStockProtocol48NormalFragmentChunkSize,
                    std::byte{0x46})));
        handshake.update(epoch + 3ms);
        REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_netchan);
        CHECK(releases == 0U);

        handshake.update(epoch + 23ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::netchan_timed_out);
        CHECK(handshake.error_context().find("fragment") != std::string_view::npos);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 23ms, releases);
    }

    SECTION("admitted fragment does not disable channel inactivity timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto config = netchan_config();
        config.first_packet_timeout = 10ms;
        config.fragment_transfer_timeout = 30ms;
        auto handshake = coordinator(
            transport,
            remote,
            prepare_with_session(releases),
            goldsrc::HandshakeStopPoint::netchan_bootstrap,
            config);
        reach_netchan_wait(transport, handshake, remote, epoch, releases);
        transport.queue(
            remote,
            normal_fragment_packet(
                1U,
                1U,
                2U,
                std::vector<std::byte>(
                    goldsrc::kStockProtocol48NormalFragmentChunkSize,
                    std::byte{0x43})));
        handshake.update(epoch + 3ms);
        REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_netchan);

        handshake.update(epoch + 13ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::netchan_timed_out);
        CHECK(handshake.error_context().find("inactivity") !=
              std::string_view::npos);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 13ms, releases);
    }

    SECTION("cancel while waiting")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_netchan_wait(transport, handshake, remote, epoch, releases);

        handshake.cancel(epoch + 3ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::cancelled);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 3ms, releases);
    }

    SECTION("malformed sequenced packet")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_netchan_wait(transport, handshake, remote, epoch, releases);

        transport.queue(
            remote,
            bytes(std::array<std::uint8_t, 7U>{0U, 0U, 0U, 0U, 0U, 0U, 0U}));
        handshake.update(epoch + 3ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::protocol_error);
        CHECK(releases == 1U);
        CHECK(handshake.error_context().find(kSyntheticAuthenticationMarker) ==
              std::string_view::npos);
        check_terminal_idempotence(transport, handshake, epoch + 3ms, releases);
    }

    SECTION("netchan receive failure")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_netchan_wait(transport, handshake, remote, epoch, releases);

        transport.queue_receive_error();
        handshake.update(epoch + 3ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::network_error);
        CHECK(releases == 1U);
        CHECK(handshake.error_context().find(kSyntheticAuthenticationMarker) ==
              std::string_view::npos);
        check_terminal_idempotence(transport, handshake, epoch + 3ms, releases);
    }

    SECTION("acknowledgement send failure")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_netchan_wait(transport, handshake, remote, epoch, releases);
        transport.failing_send_call = 3U;
        const auto opaque_payload = bytes(std::array<std::uint8_t, 4U>{
            0xffU,
            0x00U,
            0x80U,
            0x7fU,
        });
        transport.queue(remote, unfragmented_server_packet(opaque_payload));

        handshake.update(epoch + 3ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::network_error);
        CHECK_FALSE(handshake.netchan_bootstrap_result().has_value());
        CHECK(transport.sent.size() == 3U);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 3ms, releases);
    }

    SECTION("fragmented first payload reassembles before terminal success")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_netchan_wait(transport, handshake, remote, epoch, releases);

        transport.queue(remote, fragmented_server_packet());
        handshake.update(epoch + 3ms);
        CHECK(handshake.state() ==
              GoldSrcHandshakeState::netchan_bootstrap_complete);
        REQUIRE(handshake.netchan_bootstrap_result().has_value());
        CHECK(handshake.netchan_bootstrap_result()->payload.bytes ==
              bytes(std::array<std::uint8_t, 4U>{
                  0x90U,
                  0x91U,
                  0x92U,
                  0x93U,
              }));
        CHECK(transport.sent.size() == 3U);
        CHECK(handshake.netchan_session() != nullptr);
        CHECK(handshake.netchan_session()->first_acknowledgement_sent());
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 3ms, releases);
    }
}

TEST_CASE("Connect-response stop point remains terminal after ACCEPT with netchan queued",
          "[goldsrc][handshake][connect-response][compatibility]")
{
    FakeTransport transport;
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    std::size_t releases = 0U;
    auto handshake = coordinator(
        transport,
        remote,
        prepare_with_session(releases),
        goldsrc::HandshakeStopPoint::connect_response);
    reach_response_wait(transport, handshake, remote, epoch, releases);

    const auto opaque_payload = bytes(std::array<std::uint8_t, 4U>{
        0xffU,
        0x00U,
        0x80U,
        0x7fU,
    });
    transport.queue(remote, accepted_response());
    transport.queue(remote, unfragmented_server_packet(opaque_payload));
    handshake.update(epoch + 2ms);

    REQUIRE(handshake.state() == GoldSrcHandshakeState::accepted);
    REQUIRE(handshake.terminal());
    REQUIRE(handshake.connect_response().has_value());
    CHECK(std::holds_alternative<goldsrc::ConnectAccepted>(
        *handshake.connect_response()));
    CHECK_FALSE(handshake.netchan_bootstrap_result().has_value());
    CHECK(handshake.connect_send_attempts() == 1U);
    CHECK(transport.sent.size() == 2U);
    CHECK(transport.incoming.size() == 1U);
    CHECK(releases == 1U);

    check_terminal_idempotence(transport, handshake, epoch + 2ms, releases);
    CHECK(transport.incoming.size() == 1U);
}

TEST_CASE("Challenge and connect-request stop points keep their prior terminal boundaries",
          "[goldsrc][handshake][compatibility]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("challenge only")
    {
        FakeTransport transport;
        GoldSrcHandshakeCoordinator handshake{
            transport,
            remote,
            goldsrc::HandshakeStopPoint::challenge,
            std::nullopt,
            challenge_config(),
        };
        REQUIRE(handshake.start(epoch));
        transport.queue(remote, challenge_response(77U));
        handshake.update(epoch + 1ms);
        REQUIRE(handshake.state() == GoldSrcHandshakeState::challenge_received);
        CHECK(handshake.terminal());
        CHECK(transport.sent.size() == 1U);
        CHECK(handshake.connect_send_attempts() == 0U);
        REQUIRE(handshake.local_endpoint().has_value());
        CHECK(*handshake.local_endpoint() == transport.local);

        const auto sent = transport.sent.size();
        const auto received = transport.receive_calls;
        handshake.update(epoch + 2ms);
        handshake.cancel(epoch + 3ms);
        CHECK(transport.sent.size() == sent);
        CHECK(transport.receive_calls == received);
        CHECK(handshake.state() == GoldSrcHandshakeState::challenge_received);
    }

    SECTION("connect request")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(
            transport,
            remote,
            prepare_with_session(releases),
            goldsrc::HandshakeStopPoint::connect_request);
        REQUIRE(handshake.start(epoch));
        transport.queue(remote, challenge_response(78U));
        transport.queue(remote, accepted_response());
        handshake.update(epoch + 1ms);

        REQUIRE(handshake.state() == GoldSrcHandshakeState::request_sent);
        CHECK(handshake.terminal());
        CHECK(handshake.connect_send_attempts() == 1U);
        CHECK(transport.sent.size() == 2U);
        CHECK(transport.incoming.size() == 1U);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 1ms, releases);
    }
}

} // namespace
