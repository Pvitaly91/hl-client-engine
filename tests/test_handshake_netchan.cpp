#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
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
        if (changed_local_address_call &&
            local_address_calls == *changed_local_address_call) {
            return DatagramLocalAddressResult{changed_local, {}};
        }
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
    NetworkAddress changed_local{NetworkAddress::loopback(30'001)};
    std::optional<std::size_t> changed_local_address_call;
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

[[nodiscard]] std::vector<std::byte> signon_service_envelope_fixture()
{
    // Independent BZ2-NUL fixture: opcode 8, forty sanitized text bytes, NUL,
    // opcode 11, then two opaque boundary-body bytes.
    return bytes(std::array<std::uint8_t, 87U>{
        0x42U, 0x5AU, 0x32U, 0x00U,
        0x42U, 0x5AU, 0x68U, 0x39U, 0x31U, 0x41U, 0x59U, 0x26U,
        0x53U, 0x59U, 0xAFU, 0x5DU, 0x04U, 0xC7U, 0x00U, 0x00U,
        0x00U, 0xCEU, 0x18U, 0x40U, 0x48U, 0x44U, 0x00U, 0x1AU,
        0x6DU, 0x9CU, 0x60U, 0x80U, 0x10U, 0x00U, 0x08U, 0x20U,
        0x00U, 0x23U, 0x1EU, 0x6AU, 0x6AU, 0x7AU, 0x04U, 0x68U,
        0xF3U, 0x52U, 0x14U, 0x68U, 0xC8U, 0x1AU, 0x34U, 0xC8U,
        0xD2U, 0x76U, 0xBDU, 0x0EU, 0x34U, 0x99U, 0x8EU, 0xD3U,
        0x9CU, 0xB1U, 0x41U, 0x14U, 0x57U, 0xC6U, 0x9CU, 0x40U,
        0x95U, 0x20U, 0x43U, 0x19U, 0xBEU, 0x0BU, 0x39U, 0xDFU,
        0xE2U, 0xEEU, 0x48U, 0xA7U, 0x0AU, 0x12U, 0x15U, 0xEBU,
        0xA0U, 0x98U, 0xE0U,
    });
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
    const auto source = std::as_bytes(std::span{value.data(), value.size()});
    output.insert(output.end(), source.begin(), source.end());
    output.push_back(std::byte{0U});
}

[[nodiscard]] std::vector<std::byte> pre_resource_semantic_fixture(
    const bool include_leading_text = true,
    const std::uint8_t maximum_clients = 8U)
{
    std::vector<std::byte> output;
    if (include_leading_text) {
        output.push_back(std::byte{8U});
        append_nul_string(output, "x");
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
    append_nul_string(output, "sample");
    append_nul_string(output, "Local Test");
    append_nul_string(output, "maps/test_alpha.bsp");
    append_nul_string(output, "alpha beta");
    output.push_back(std::byte{0U});
    output.push_back(
        static_cast<std::byte>(goldsrc::kPreResourceSimpleControlOpcode));
    output.push_back(std::byte{0U});
    output.push_back(std::byte{0U});
    output.push_back(
        static_cast<std::byte>(goldsrc::kPreResourceComplexBoundaryOpcode));
    output.push_back(std::byte{0xa5U});
    output.push_back(std::byte{0x5aU});
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

    std::vector<std::byte> envelope{
        std::byte{0x42U},
        std::byte{0x5aU},
        std::byte{0x32U},
        std::byte{0U},
    };
    std::ranges::transform(
        compressed,
        std::back_inserter(envelope),
        [](const char value) {
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> signon_server_packet(
    std::vector<std::byte> payload)
{
    return encode_server_packet(goldsrc::ServerToClientNetchanPacket{
        server_header(1U, true, false, 1U, true),
        {},
        std::move(payload),
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

[[nodiscard]] GoldSrcHandshakeCoordinator signon_coordinator(
    FakeTransport& transport,
    const NetworkAddress remote,
    PreparedWithSession prepared,
    goldsrc::InitialSignonConfig signon_config = {})
{
    return GoldSrcHandshakeCoordinator{
        transport,
        remote,
        goldsrc::HandshakeStopPoint::signon_boundary,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {},
        {},
        std::move(signon_config),
        {},
    };
}

[[nodiscard]] goldsrc::PreResourceSignonConfig pre_resource_config()
{
    goldsrc::PreResourceSignonConfig config;
    config.initial_signon.driver.channel_inactivity_timeout = 50ms;
    config.initial_signon.driver.fragment_transfer_timeout = 50ms;
    config.initial_signon.driver.maximum_datagrams_per_update = 8U;
    config.initial_signon.driver.maximum_outgoing_packets_per_update = 8U;
    config.initial_signon.driver.maximum_events = 32U;
    config.initial_signon.maximum_events = 32U;
    config.initial_signon.maximum_driver_events_per_update = 32U;
    config.maximum_events = 32U;
    return config;
}

[[nodiscard]] GoldSrcHandshakeCoordinator pre_resource_coordinator(
    FakeTransport& transport,
    const NetworkAddress remote,
    PreparedWithSession prepared,
    goldsrc::PreResourceSignonConfig config = pre_resource_config())
{
    return GoldSrcHandshakeCoordinator{
        transport,
        remote,
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
        std::move(config),
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

TEST_CASE("Sign-on stop point owns the same socket and authentication lifetime through boundary",
          "[goldsrc][handshake][signon][coordinator][auth]")
{
    FakeTransport transport;
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    std::size_t releases = 0U;
    auto handshake = signon_coordinator(
        transport,
        remote,
        prepare_with_session(releases));

    reach_response_wait(transport, handshake, remote, epoch, releases);
    transport.queue(remote, accepted_response());
    handshake.update(epoch + 2ms);

    REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_signon);
    REQUIRE_FALSE(handshake.terminal());
    CHECK(transport.sent.size() == 2U);
    CHECK(releases == 0U);
    CHECK_FALSE(handshake.netchan_bootstrap_result().has_value());
    CHECK_FALSE(handshake.initial_signon_result().has_value());

    handshake.update(epoch + 3ms);
    REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_signon);
    REQUIRE(transport.sent.size() == 3U);
    const auto request = decode_client_packet(transport.sent[2U]);
    CHECK(request.header.sequence.sequence.value() == 1U);
    CHECK(request.header.sequence.flags.reliable);
    CHECK_FALSE(request.header.sequence.flags.fragmented);
    CHECK(request.header.acknowledgement.sequence.value() == 0U);
    CHECK_FALSE(request.header.acknowledgement.reliable);
    REQUIRE(request.payload.size() == goldsrc::kStockProtocol48MinimumDecodedPayloadSize);
    CHECK(request.payload[0U] == std::byte{0x03U});
    CHECK(request.payload[1U] == std::byte{'n'});
    CHECK(request.payload[2U] == std::byte{'e'});
    CHECK(request.payload[3U] == std::byte{'w'});
    CHECK(request.payload[4U] == std::byte{0x00U});
    CHECK(std::ranges::all_of(
        std::span<const std::byte>{request.payload}.subspan(5U),
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
    CHECK(releases == 0U);

    transport.queue(remote, signon_server_packet(signon_service_envelope_fixture()));
    handshake.update(epoch + 4ms);

    REQUIRE(handshake.state() == GoldSrcHandshakeState::signon_boundary_reached);
    REQUIRE(handshake.terminal());
    CHECK(releases == 1U);
    REQUIRE(transport.sent.size() == 4U);
    const auto acknowledgement = decode_client_packet(transport.sent[3U]);
    CHECK(acknowledgement.header.sequence.sequence.value() == 2U);
    CHECK_FALSE(acknowledgement.header.sequence.flags.reliable);
    CHECK(acknowledgement.header.acknowledgement.sequence.value() == 1U);
    CHECK(acknowledgement.header.acknowledgement.reliable);
    CHECK(std::ranges::all_of(
        acknowledgement.payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));

    REQUIRE(handshake.initial_signon_result().has_value());
    const auto& result = *handshake.initial_signon_result();
    REQUIRE(result.messages.size() == 1U);
    CHECK(result.messages.front().opcode ==
          goldsrc::ServiceMessageOpcode::text_control);
    CHECK(result.boundary.opcode ==
          goldsrc::ServiceMessageOpcode::complex_signon_boundary);
    CHECK(result.boundary.byte_offset == 42U);
    CHECK(result.boundary.remaining_byte_count == 2U);
    CHECK(result.boundary_payload.decompressed);
    CHECK(result.boundary_payload.bytes.size() == 45U);
    CHECK(handshake.error_context().empty());

    check_terminal_idempotence(transport, handshake, epoch + 4ms, releases);
    CHECK(transport.sent.size() == 4U);
}

TEST_CASE("Sign-on coordinator terminalizes transactional stage-start failures",
          "[goldsrc][handshake][signon][coordinator][auth][error]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("invalid sign-on configuration")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::InitialSignonConfig config;
        config.maximum_events = 0U;
        auto handshake = signon_coordinator(
            transport,
            remote,
            prepare_with_session(releases),
            config);

        reach_response_wait(transport, handshake, remote, epoch, releases);
        transport.queue(remote, accepted_response());
        handshake.update(epoch + 2ms);

        CHECK(handshake.state() == GoldSrcHandshakeState::configuration_error);
        CHECK(handshake.terminal());
        REQUIRE(handshake.initial_signon_error().has_value());
        CHECK(handshake.initial_signon_error()->code ==
              goldsrc::InitialSignonErrorCode::invalid_configuration);
        CHECK(transport.sent.size() == 2U);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 2ms, releases);
    }

    SECTION("same-socket local endpoint changes before driver start")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = signon_coordinator(
            transport,
            remote,
            prepare_with_session(releases));

        reach_response_wait(transport, handshake, remote, epoch, releases);
        transport.changed_local_address_call = transport.local_address_calls + 1U;
        transport.queue(remote, accepted_response());
        handshake.update(epoch + 2ms);

        CHECK(handshake.state() == GoldSrcHandshakeState::network_error);
        CHECK(handshake.terminal());
        REQUIRE(handshake.initial_signon_error().has_value());
        CHECK(handshake.initial_signon_error()->code ==
              goldsrc::InitialSignonErrorCode::driver_start_failed);
        CHECK(handshake.initial_signon_error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::local_endpoint_changed);
        CHECK(transport.sent.size() == 2U);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 2ms, releases);
    }
}

TEST_CASE("Pre-resource stop point preserves one socket and authentication lifetime",
          "[goldsrc][handshake][pre-resource][coordinator][auth]")
{
    FakeTransport transport;
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    std::size_t releases = 0U;
    auto handshake = pre_resource_coordinator(
        transport,
        remote,
        prepare_with_session(releases));

    reach_response_wait(transport, handshake, remote, epoch, releases);
    transport.queue(remote, accepted_response());
    handshake.update(epoch + 2ms);

    REQUIRE(handshake.state() ==
            GoldSrcHandshakeState::waiting_for_pre_resource);
    REQUIRE_FALSE(handshake.terminal());
    CHECK(transport.sent.size() == 2U);
    CHECK(releases == 0U);
    CHECK_FALSE(handshake.initial_signon_result());
    CHECK_FALSE(handshake.pre_resource_result());

    handshake.update(epoch + 3ms);
    REQUIRE(handshake.state() ==
            GoldSrcHandshakeState::waiting_for_pre_resource);
    REQUIRE(transport.sent.size() == 3U);
    const auto request = decode_client_packet(transport.sent[2U]);
    CHECK(request.header.sequence.sequence.value() == 1U);
    CHECK(request.header.sequence.flags.reliable);
    CHECK(request.payload[0U] == std::byte{3U});
    CHECK(request.payload[1U] == std::byte{'n'});
    CHECK(request.payload[2U] == std::byte{'e'});
    CHECK(request.payload[3U] == std::byte{'w'});
    CHECK(request.payload[4U] == std::byte{0U});
    CHECK(releases == 0U);

    const auto semantic = pre_resource_semantic_fixture();
    transport.queue(
        remote,
        signon_server_packet(service_envelope(semantic)));
    handshake.update(epoch + 4ms);

    REQUIRE(handshake.state() ==
            GoldSrcHandshakeState::pre_resource_boundary_reached);
    REQUIRE(handshake.terminal());
    CHECK(releases == 1U);
    REQUIRE(transport.sent.size() == 4U);
    const auto acknowledgement = decode_client_packet(transport.sent[3U]);
    CHECK(acknowledgement.header.sequence.sequence.value() == 2U);
    CHECK_FALSE(acknowledgement.header.sequence.flags.reliable);
    CHECK(acknowledgement.header.acknowledgement.sequence.value() == 1U);
    CHECK(acknowledgement.header.acknowledgement.reliable);
    CHECK(std::ranges::all_of(
        acknowledgement.payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));

    CHECK_FALSE(handshake.initial_signon_result());
    REQUIRE(handshake.pre_resource_result());
    const auto& result = *handshake.pre_resource_result();
    CHECK(result.server_info().protocol_version() ==
          goldsrc::ProtocolVersion::goldsrc_48);
    CHECK(result.server_info().maximum_clients().value() == 8U);
    CHECK(result.server_info().multi_client_mode());
    REQUIRE(result.controls().size() == 1U);
    CHECK(result.controls().front().opcode() ==
          goldsrc::kPreResourceSimpleControlOpcode);
    CHECK(result.boundary().opcode() ==
          goldsrc::kPreResourceComplexBoundaryOpcode);
    CHECK(result.boundary().byte_offset() == 88U);
    CHECK(result.boundary().remaining_byte_count() == 2U);
    CHECK(result.boundary().direction() ==
          goldsrc::ResourcePhaseBoundaryDirection::server_message);
    CHECK(result.source_payload().initial_boundary_offset() == 3U);
    CHECK(result.source_payload().server_info_body_offset() == 4U);
    CHECK(result.source_payload().payload_size() == semantic.size());
    CHECK(handshake.error_context().empty());

    check_terminal_idempotence(transport, handshake, epoch + 4ms, releases);
    CHECK(transport.sent.size() == 4U);
}

TEST_CASE("Pre-resource coordinator terminalizes transactional stage-start failures",
          "[goldsrc][handshake][pre-resource][coordinator][auth][error]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("invalid pre-resource configuration")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto config = pre_resource_config();
        config.maximum_events = 0U;
        auto handshake = pre_resource_coordinator(
            transport,
            remote,
            prepare_with_session(releases),
            config);

        reach_response_wait(transport, handshake, remote, epoch, releases);
        transport.queue(remote, accepted_response());
        handshake.update(epoch + 2ms);

        CHECK(handshake.state() == GoldSrcHandshakeState::configuration_error);
        CHECK(handshake.terminal());
        REQUIRE(handshake.pre_resource_error());
        CHECK(handshake.pre_resource_error()->code ==
              goldsrc::PreResourceSignonErrorCode::invalid_configuration);
        CHECK(transport.sent.size() == 2U);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 2ms, releases);
    }

    SECTION("same-socket local endpoint changes before nested driver start")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = pre_resource_coordinator(
            transport,
            remote,
            prepare_with_session(releases));

        reach_response_wait(transport, handshake, remote, epoch, releases);
        transport.changed_local_address_call = transport.local_address_calls + 1U;
        transport.queue(remote, accepted_response());
        handshake.update(epoch + 2ms);

        CHECK(handshake.state() == GoldSrcHandshakeState::network_error);
        CHECK(handshake.terminal());
        REQUIRE(handshake.pre_resource_error());
        CHECK(handshake.pre_resource_error()->code ==
              goldsrc::PreResourceSignonErrorCode::initial_signon_start_failed);
        CHECK(handshake.pre_resource_error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::local_endpoint_changed);
        CHECK(transport.sent.size() == 2U);
        CHECK(releases == 1U);
        check_terminal_idempotence(transport, handshake, epoch + 2ms, releases);
    }
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
