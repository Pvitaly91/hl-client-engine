#include <hlclient/goldsrc/initial_signon_stage.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

static_assert(!std::is_copy_constructible_v<goldsrc::InitialSignonStage>);
static_assert(!std::is_move_constructible_v<goldsrc::InitialSignonStage>);

class CountingLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept : releases_{releases} {}
    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

class FakeTransport final : public network::IDatagramTransport {
public:
    struct SentDatagram {
        network::NetworkAddress destination;
        std::vector<std::byte> payload;
    };

    [[nodiscard]] network::DatagramLocalAddressResult local_address() const override
    {
        ++local_queries;
        if (throw_on_local) {
            throw std::runtime_error{"synthetic local-address exception"};
        }
        return network::DatagramLocalAddressResult{local, local_error};
    }

    [[nodiscard]] network::DatagramSendResult send_to(
        const network::NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        ++send_attempts;
        if (throw_on_send) {
            throw std::runtime_error{"synthetic send exception"};
        }
        sent.push_back(SentDatagram{
            destination,
            std::vector<std::byte>{payload.begin(), payload.end()},
        });
        return network::DatagramSendResult{network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        const std::size_t maximum_size) override
    {
        ++receive_calls;
        receive_limits.push_back(maximum_size);
        if (throw_on_receive) {
            throw std::runtime_error{"synthetic receive exception"};
        }
        if (incoming.empty()) {
            return network::DatagramTransportReceiveResult{
                network::DatagramTransportReceiveStatus::would_block,
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

    void queue(const network::NetworkAddress source, std::vector<std::byte> payload)
    {
        const auto payload_size = payload.size();
        incoming.push_back(network::DatagramTransportReceiveResult{
            network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(payload)},
            source,
            payload_size,
            {},
        });
    }

    std::optional<network::NetworkAddress> local{
        network::NetworkAddress::loopback(31'200U)};
    std::string local_error;
    mutable bool throw_on_local{false};
    bool throw_on_receive{false};
    bool throw_on_send{false};
    mutable std::size_t local_queries{0U};
    std::size_t send_attempts{0U};
    std::size_t receive_calls{0U};
    std::vector<std::size_t> receive_limits;
    std::deque<network::DatagramTransportReceiveResult> incoming;
    std::vector<SentDatagram> sent;
};

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    if (!parsed) {
        throw std::runtime_error{"invalid synthetic sequence"};
    }
    return *parsed;
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

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto raw = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{raw.begin(), raw.end()};
}

// Independent synthetic fixture builder. It uses the standard bzip2 API and
// literal wire bytes, not either production GoldSrc decoder.
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
    const auto result = BZ2_bzBuffToBuffCompress(
        compressed.data(),
        &compressed_size,
        source.data(),
        static_cast<unsigned int>(source.size()),
        9,
        0,
        30);
    REQUIRE(result == BZ_OK);
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

[[nodiscard]] std::vector<std::byte> synthetic_boundary_payload()
{
    auto payload = bytes({0x08U});
    const auto text = bytes("FAKE-HLDS EARLY TEXT");
    payload.insert(payload.end(), text.begin(), text.end());
    payload.push_back(std::byte{0U});
    payload.push_back(std::byte{0x0bU});
    payload.insert(
        payload.end(),
        {std::byte{0xdeU}, std::byte{0xadU}, std::byte{0xbeU}, std::byte{0xefU}});
    return payload;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t packet_sequence,
    const bool reliable,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    std::vector<std::byte> payload)
{
    goldsrc::ServerToClientNetchanPacket packet{
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
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"unable to encode synthetic server packet"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> secondary_fragment_packet()
{
    goldsrc::NetchanFragmentSlots fragments;
    fragments[1U] = goldsrc::NetchanFragmentDescriptor{
        1U,
        (1U << 16U) | 1U,
        0U,
        1U,
        0U,
    };
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(1U),
                goldsrc::NetchanSequenceFlags{true, true},
            },
            goldsrc::NetchanAcknowledgementWord{sequence(0U), false},
        },
        std::move(fragments),
        bytes({0x41U}),
        1U,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"unable to encode secondary fragment"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> normal_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const std::uint16_t fragment_index,
    std::vector<std::byte> fragment_payload)
{
    REQUIRE_FALSE(fragment_payload.empty());
    REQUIRE(fragment_payload.size() <=
            (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_size = fragment_payload.size();
    goldsrc::NetchanFragmentSlots fragments;
    fragments[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        (static_cast<std::uint32_t>(fragment_index) << 16U) | 2U,
        0U,
        static_cast<std::uint16_t>(fragment_size),
        0U,
    };
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{true, true},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                false,
            },
        },
        std::move(fragments),
        std::move(fragment_payload),
        fragment_size,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"unable to encode normal fragment"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::InitialSignonConfig test_config()
{
    goldsrc::InitialSignonConfig config;
    config.driver.channel_inactivity_timeout = 100ms;
    config.driver.fragment_transfer_timeout = 50ms;
    config.driver.maximum_datagrams_per_update = 8U;
    config.driver.maximum_outgoing_packets_per_update = 8U;
    config.driver.maximum_events = 32U;
    config.maximum_events = 32U;
    config.maximum_driver_events_per_update = 32U;
    return config;
}

void require_started(
    goldsrc::InitialSignonStage& stage,
    FakeTransport& transport,
    const goldsrc::InitialSignonTimePoint now,
    std::unique_ptr<goldsrc::INetchanDriverLifetime> lifetime = {})
{
    REQUIRE(transport.local);
    REQUIRE(stage.start(now, *transport.local, std::move(lifetime)));
    CHECK(stage.state() == goldsrc::InitialSignonState::waiting_for_request_transmit);
    CHECK(stage.local_endpoint() == transport.local);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.pending_event_count() == 1U);
    CHECK(transport.sent.empty());
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket require_initial_request_sent(
    goldsrc::InitialSignonStage& stage,
    FakeTransport& transport,
    const goldsrc::InitialSignonTimePoint now)
{
    stage.update(now);
    REQUIRE(stage.state() == goldsrc::InitialSignonState::waiting_for_request_ack);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent.front().destination == stage.remote_endpoint());
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.sequence == sequence(1U));
    CHECK(decoded.packet->header.sequence.flags.reliable);
    CHECK_FALSE(decoded.packet->header.sequence.flags.fragmented);
    CHECK(decoded.packet->header.acknowledgement.sequence == sequence(0U));
    const auto exact_request = bytes({0x03U, 0x6eU, 0x65U, 0x77U, 0x00U});
    REQUIRE(decoded.packet->payload.size() == 8U);
    CHECK(std::ranges::equal(
        exact_request,
        std::span<const std::byte>{decoded.packet->payload}.first(
            exact_request.size())));
    CHECK(std::ranges::all_of(
        std::span<const std::byte>{decoded.packet->payload}.subspan(
            exact_request.size()),
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
    REQUIRE(stage.driver());
    REQUIRE(stage.driver()->session().in_flight_reliable_payload());
    CHECK(stage.driver()->session().in_flight_reliable_payload()->bytes ==
          exact_request);
    CHECK(stage.request_queue_count() == 1U);
    return std::move(*decoded.packet);
}

TEST_CASE("Initial sign-on stage configuration has positive hard limits",
          "[goldsrc][signon][stage][limits]")
{
    CHECK(goldsrc::valid_initial_signon_configuration(test_config()));

    auto config = test_config();
    config.maximum_events = goldsrc::kMaximumInitialSignonEvents;
    CHECK(goldsrc::valid_initial_signon_configuration(config));
    config.maximum_events = 0U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
    config = test_config();
    config.maximum_events = goldsrc::kMaximumInitialSignonEvents + 1U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
    config = test_config();
    config.maximum_driver_events_per_update =
        goldsrc::kMaximumInitialSignonDriverEventsPerUpdate;
    CHECK(goldsrc::valid_initial_signon_configuration(config));
    config.maximum_driver_events_per_update = 0U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
    config = test_config();
    config.maximum_driver_events_per_update =
        goldsrc::kMaximumInitialSignonDriverEventsPerUpdate + 1U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
    config = test_config();
    config.service_messages.maximum_payload_size = 0U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
}

TEST_CASE("Initial sign-on start queues one exact typed request and first update sends it",
          "[goldsrc][signon][stage][request][exact]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);

    auto queued = stage.poll_event();
    REQUIRE(queued);
    CHECK(queued->type == goldsrc::InitialSignonEventType::initial_request_queued);
    CHECK(queued->byte_count == goldsrc::kInitialSignonRequestSize);
    const auto packet = require_initial_request_sent(stage, transport, epoch + 1ms);
    CHECK(packet.payload.size() == goldsrc::kStockProtocol48MinimumDecodedPayloadSize);
    auto transmitted = stage.poll_event();
    REQUIRE(transmitted);
    CHECK(transmitted->type ==
          goldsrc::InitialSignonEventType::initial_request_transmitted);
    CHECK_FALSE(stage.poll_event());
}

TEST_CASE("Initial sign-on failed start is transactional and releases lifetime once",
          "[goldsrc][signon][stage][start][lifetime]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    REQUIRE(transport.local);
    const auto wrong_local = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(transport.local->port() + 1U));

    CHECK_FALSE(stage.start(
        epoch,
        wrong_local,
        std::make_unique<CountingLifetime>(releases)));
    CHECK(stage.state() == goldsrc::InitialSignonState::idle);
    CHECK_FALSE(stage.terminal());
    CHECK(stage.request_queue_count() == 0U);
    CHECK(stage.driver() == nullptr);
    CHECK(transport.sent.empty());
    CHECK(releases == 1U);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::InitialSignonErrorCode::driver_start_failed);
}

TEST_CASE("Repeated initial sign-on start rejects an active stage transactionally",
          "[goldsrc][signon][stage][start][active][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    std::size_t active_releases = 0U;
    std::size_t rejected_releases = 0U;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(active_releases));
    REQUIRE(transport.local);

    CHECK_FALSE(stage.start(
        epoch + 1ms,
        *transport.local,
        std::make_unique<CountingLifetime>(rejected_releases)));
    CHECK(stage.state() ==
          goldsrc::InitialSignonState::waiting_for_request_transmit);
    CHECK_FALSE(stage.terminal());
    CHECK_FALSE(stage.error());
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.pending_event_count() == 1U);
    CHECK(transport.sent.empty());
    CHECK(active_releases == 0U);
    CHECK(rejected_releases == 1U);

    static_cast<void>(require_initial_request_sent(
        stage,
        transport,
        epoch + 2ms));
    CHECK(transport.sent.size() == 1U);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(active_releases == 0U);
    CHECK(rejected_releases == 1U);

    stage.cancel(epoch + 3ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::cancelled);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(active_releases == 1U);
    CHECK(rejected_releases == 1U);
    const auto sends_at_cancel = transport.sent.size();
    stage.update(epoch + 4ms);
    stage.cancel(epoch + 5ms);
    CHECK(transport.sent.size() == sends_at_cancel);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(active_releases == 1U);
    CHECK(rejected_releases == 1U);
}

TEST_CASE("Initial sign-on retransmission stays in the driver and never requeues semantics",
          "[goldsrc][signon][stage][request][retry]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    const auto first = require_initial_request_sent(stage, transport, epoch + 1ms);

    // Cover the numeric sequence with the old generation. The existing
    // reliable lifecycle requests an exact retransmission; the stage does not
    // queue another semantic request.
    transport.queue(
        remote,
        normal_fragment_packet(
            1U,
            first.header.sequence.sequence.value(),
            2U,
            bytes({0x42U})));
    stage.update(epoch + 2ms);
    REQUIRE(stage.state() == goldsrc::InitialSignonState::waiting_for_request_ack);
    CHECK(stage.request_queue_count() == 1U);
    REQUIRE(transport.sent.size() == 2U); // request + first fragment ACK

    transport.queue(
        remote,
        normal_fragment_packet(
            2U,
            2U,
            1U,
            std::vector<std::byte>(
                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                std::byte{0x41U})));
    stage.update(epoch + 3ms);
    REQUIRE(stage.state() == goldsrc::InitialSignonState::waiting_for_request_ack);
    // The second fragment ACK piggybacks the exact reliable retry.
    REQUIRE(transport.sent.size() == 3U);

    std::size_t semantic_request_packets = 0U;
    std::optional<goldsrc::ClientToServerNetchanPacket> retry;
    for (const auto& sent : transport.sent) {
        const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
            sent.payload);
        REQUIRE(decoded);
        REQUIRE(decoded.packet);
        if (decoded.packet->header.sequence.flags.reliable) {
            ++semantic_request_packets;
            retry = *decoded.packet;
        }
    }
    CHECK(semantic_request_packets == 2U);
    REQUIRE(retry);
    CHECK(retry->header.sequence.sequence != first.header.sequence.sequence);
    CHECK(retry->payload == first.payload);
    REQUIRE(stage.driver());
    REQUIRE(stage.driver()->session().in_flight_reliable_payload());
    CHECK(stage.driver()->session().in_flight_reliable_payload()->send_count == 2U);
}

TEST_CASE("Initial sign-on buffers at most one owning payload before request ACK",
          "[goldsrc][signon][stage][payload][pre-ack][bounds]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    static_cast<void>(require_initial_request_sent(stage, transport, epoch + 1ms));

    transport.queue(remote, server_packet(1U, false, 0U, false, bytes({0x41U})));
    transport.queue(remote, server_packet(2U, false, 0U, false, bytes({0x42U})));
    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::InitialSignonErrorCode::service_payload_before_ack_overflow);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(stage.request_queue_count() == 1U);
}

TEST_CASE("Initial sign-on reaches the owning boundary only after the matching ACK",
          "[goldsrc][signon][stage][ack][boundary][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    static_cast<void>(stage.poll_event());
    const auto first = require_initial_request_sent(stage, transport, epoch + 1ms);
    static_cast<void>(stage.poll_event());

    const auto semantic = synthetic_boundary_payload();
    transport.queue(
        remote,
        server_packet(
            1U,
            false,
            first.header.sequence.sequence.value(),
            true,
            service_envelope(semantic)));
    stage.update(epoch + 2ms);

    CHECK(stage.state() == goldsrc::InitialSignonState::signon_boundary_reached);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->messages.size() == 1U);
    const auto& message = stage.result()->messages.front();
    CHECK(message.opcode == goldsrc::ServiceMessageOpcode::text_control);
    CHECK(message.kind == goldsrc::ServiceMessageKind::text_control);
    CHECK(message.byte_offset == 0U);
    REQUIRE(std::holds_alternative<goldsrc::ServiceTextControl>(message.body));
    CHECK(std::get<goldsrc::ServiceTextControl>(message.body).text ==
          "FAKE-HLDS EARLY TEXT");
    CHECK(stage.result()->boundary.opcode ==
          goldsrc::ServiceMessageOpcode::complex_signon_boundary);
    CHECK(stage.result()->boundary.byte_offset == 22U);
    CHECK(stage.result()->boundary.remaining_byte_count == 4U);
    CHECK(stage.result()->boundary_payload.bytes == semantic);
    CHECK(stage.result()->boundary_payload.decompressed);
    CHECK_FALSE(stage.result()->boundary_payload.reassembled);
    CHECK(stage.result()->service_payload_count == 1U);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    std::vector<goldsrc::InitialSignonEventType> event_types;
    while (auto event = stage.poll_event()) {
        event_types.push_back(event->type);
    }
    CHECK(event_types == std::vector<goldsrc::InitialSignonEventType>{
                             goldsrc::InitialSignonEventType::
                                 initial_request_acknowledged,
                             goldsrc::InitialSignonEventType::
                                 service_payload_received,
                             goldsrc::InitialSignonEventType::
                                 service_message_decoded,
                             goldsrc::InitialSignonEventType::
                                 signon_boundary_reached,
                         });
    const auto sends_at_boundary = transport.sent.size();
    stage.update(epoch + 3ms);
    CHECK(transport.sent.size() == sends_at_boundary);
}

TEST_CASE("Initial sign-on keeps one pre-ACK payload owning then decodes it after ACK",
          "[goldsrc][signon][stage][payload][pre-ack][ack]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    const auto first = require_initial_request_sent(stage, transport, epoch + 1ms);

    // A valid owning payload arrives with the wrong reliable ACK generation.
    // It must not be decoded or published yet.
    const auto semantic = synthetic_boundary_payload();
    transport.queue(
        remote,
        server_packet(
            1U,
            false,
            first.header.sequence.sequence.value(),
            false,
            service_envelope(semantic)));
    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::waiting_for_request_ack);
    CHECK_FALSE(stage.result());
    CHECK(stage.request_queue_count() == 1U);
    REQUIRE(stage.driver());
    REQUIRE(stage.driver()->session().in_flight_reliable_payload());
    CHECK(stage.driver()->session().in_flight_reliable_payload()->send_count == 1U);

    // The following header-only transport unit carries the covering generation.
    // Its ordinary padding payload remains queued behind terminal success and
    // is never interpreted as another semantic batch.
    transport.queue(
        remote,
        server_packet(2U, false, 2U, true, {}));
    stage.update(epoch + 3ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::signon_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->boundary_payload.bytes == semantic);
    CHECK(stage.request_queue_count() == 1U);
}

TEST_CASE("Dropped request ACK is completed by the server retransmission without client requeue",
          "[goldsrc][signon][stage][ack][drop-ack]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    const auto request = require_initial_request_sent(stage, transport, epoch + 1ms);

    // Synthetic server packet sequence 1, which carried the request ACK, was
    // dropped before reaching the client. Its fresh-sequence retransmission
    // retains the same covering reliable generation and complete batch.
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            request.header.sequence.sequence.value(),
            true,
            service_envelope(synthetic_boundary_payload())));
    stage.update(epoch + 2ms);
    REQUIRE(stage.state() == goldsrc::InitialSignonState::signon_boundary_reached);
    CHECK(stage.request_queue_count() == 1U);

    std::size_t request_packets = 0U;
    for (const auto& sent : transport.sent) {
        auto decoded = goldsrc::decode_client_to_server_netchan_packet(sent.payload);
        REQUIRE(decoded);
        REQUIRE(decoded.packet);
        request_packets += decoded.packet->header.sequence.flags.reliable ? 1U : 0U;
    }
    CHECK(request_packets == 1U);
}

TEST_CASE("Wrong endpoint cannot ACK or publish the initial sign-on boundary",
          "[goldsrc][signon][stage][endpoint][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto rogue = network::NetworkAddress::loopback(27'129U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    const auto request = require_initial_request_sent(stage, transport, epoch + 1ms);
    const auto response = server_packet(
        1U,
        false,
        request.header.sequence.sequence.value(),
        true,
        service_envelope(synthetic_boundary_payload()));

    transport.queue(rogue, response);
    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::waiting_for_request_ack);
    CHECK_FALSE(stage.result());
    CHECK(transport.sent.size() == 1U);

    transport.queue(remote, response);
    stage.update(epoch + 3ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::signon_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->service_payload_count == 1U);
}

TEST_CASE("Missing service fragment reaches the fixed fragment timeout without partial decode",
          "[goldsrc][signon][stage][fragment][timeout]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    const auto request = require_initial_request_sent(stage, transport, epoch + 1ms);

    transport.queue(
        remote,
        [&] {
            auto fragment = normal_fragment_packet(
                1U,
                request.header.sequence.sequence.value(),
                2U,
                bytes({0x42U}));
            // The normal helper uses old reliable ACK generation to exercise
            // retries elsewhere. Patch only the independently encoded header
            // ACK bit here; the transform key/body remain sequence-bound.
            fragment[7U] |= std::byte{0x80U};
            return fragment;
        }());
    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::waiting_for_server_payload);
    CHECK_FALSE(stage.result());
    stage.update(epoch + 52ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::timed_out);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::InitialSignonErrorCode::fragment_transfer_timed_out);
    CHECK_FALSE(stage.result());
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Duplicate whole service batch after boundary is not consumed or republished",
          "[goldsrc][signon][stage][duplicate][terminal]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    const auto request = require_initial_request_sent(stage, transport, epoch + 1ms);
    const auto response = server_packet(
        1U,
        false,
        request.header.sequence.sequence.value(),
        true,
        service_envelope(synthetic_boundary_payload()));
    transport.queue(remote, response);
    stage.update(epoch + 2ms);
    REQUIRE(stage.result());
    while (stage.poll_event()) {
    }
    const auto sends_at_boundary = transport.sent.size();

    transport.queue(remote, response);
    stage.update(epoch + 3ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::signon_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->service_payload_count == 1U);
    CHECK(stage.result()->messages.size() == 1U);
    CHECK_FALSE(stage.poll_event());
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(transport.incoming.size() == 1U);
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Initial sign-on envelope and service failures are typed and fail closed",
          "[goldsrc][signon][stage][decoder][security]")
{
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    const auto run = [&](std::vector<std::byte> payload,
                         const goldsrc::InitialSignonErrorCode expected) {
        FakeTransport transport;
        goldsrc::InitialSignonStage stage{transport, remote, test_config()};
        require_started(stage, transport, epoch);
        const auto first = require_initial_request_sent(
            stage,
            transport,
            epoch + 1ms);
        transport.queue(
            remote,
            server_packet(
                1U,
                false,
                first.header.sequence.sequence.value(),
                true,
                std::move(payload)));
        stage.update(epoch + 2ms);
        CHECK(stage.state() ==
              (expected == goldsrc::InitialSignonErrorCode::unsupported_service_opcode
                   ? goldsrc::InitialSignonState::unsupported_service_message
                   : goldsrc::InitialSignonState::protocol_error));
        REQUIRE(stage.error());
        CHECK(stage.error()->code == expected);
        CHECK_FALSE(stage.result());
        CHECK(stage.cleanup_count() == 1U);
        CHECK(stage.request_queue_count() == 1U);
    };

    run(
        bytes({0x42U, 0x5aU, 0x32U, 0x00U, 0x42U, 0x5aU}),
        goldsrc::InitialSignonErrorCode::service_payload_envelope_decode_failed);
    run(
        service_envelope(bytes({0x7fU, 0x01U})),
        goldsrc::InitialSignonErrorCode::unsupported_service_opcode);
    run(
        service_envelope(bytes({0x08U, 0x41U})),
        goldsrc::InitialSignonErrorCode::service_message_decode_failed);
    run(
        service_envelope(bytes({0x0bU})),
        goldsrc::InitialSignonErrorCode::service_message_decode_failed);
}

TEST_CASE("Initial sign-on signon-event backpressure fails before first transmit",
          "[goldsrc][signon][stage][backpressure][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_events = 1U;
    goldsrc::InitialSignonStage stage{transport, remote, config};
    require_started(stage, transport, epoch);

    stage.update(epoch + 1ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::backpressure);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::InitialSignonErrorCode::service_event_backpressure);
    CHECK(transport.sent.empty());
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Decoded sign-on batch event backpressure commits no partial semantics",
          "[goldsrc][signon][stage][backpressure][batch][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    auto config = test_config();
    // Queued + transmitted + acknowledged leave one ring slot. The decoded
    // batch needs three atomic publications: payload, simple message, boundary.
    config.maximum_events = 4U;
    std::size_t releases = 0U;
    goldsrc::InitialSignonStage stage{transport, remote, config};
    require_started(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    const auto request = require_initial_request_sent(
        stage,
        transport,
        epoch + 1ms);
    REQUIRE(stage.pending_event_count() == 2U);

    transport.queue(
        remote,
        server_packet(
            1U,
            false,
            request.header.sequence.sequence.value(),
            true,
            service_envelope(synthetic_boundary_payload())));
    stage.update(epoch + 2ms);

    CHECK(stage.state() == goldsrc::InitialSignonState::backpressure);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::InitialSignonErrorCode::service_event_backpressure);
    CHECK_FALSE(stage.result());
    CHECK(stage.pending_event_count() == 3U);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    std::vector<goldsrc::InitialSignonEventType> event_types;
    while (auto event = stage.poll_event()) {
        event_types.push_back(event->type);
    }
    CHECK(event_types == std::vector<goldsrc::InitialSignonEventType>{
                             goldsrc::InitialSignonEventType::
                                 initial_request_queued,
                             goldsrc::InitialSignonEventType::
                                 initial_request_transmitted,
                             goldsrc::InitialSignonEventType::
                                 initial_request_acknowledged,
                         });

    const auto sends_at_failure = transport.sent.size();
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.sent.size() == sends_at_failure);
    CHECK_FALSE(stage.poll_event());
    CHECK_FALSE(stage.result());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Initial sign-on channel timeout and cancellation release ownership once",
          "[goldsrc][signon][stage][timeout][cancel][lifetime]")
{
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;

    SECTION("channel timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::InitialSignonStage stage{transport, remote, test_config()};
        require_started(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases));
        static_cast<void>(require_initial_request_sent(stage, transport, epoch + 1ms));
        stage.update(epoch + 100ms);
        CHECK(stage.state() == goldsrc::InitialSignonState::timed_out);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::InitialSignonErrorCode::channel_inactivity_timed_out);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
        stage.update(epoch + 101ms);
        stage.cancel(epoch + 102ms);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("cancellation")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::InitialSignonStage stage{transport, remote, test_config()};
        require_started(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases));
        stage.cancel(epoch + 1ms);
        CHECK(stage.state() == goldsrc::InitialSignonState::cancelled);
        CHECK(stage.terminal());
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
        stage.cancel(epoch + 2ms);
        stage.update(epoch + 3ms);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
        CHECK(transport.sent.empty());
    }
}

TEST_CASE("Initial sign-on maps secondary stream to a typed pending-M3 outcome",
          "[goldsrc][signon][stage][secondary][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    static_cast<void>(require_initial_request_sent(stage, transport, epoch + 1ms));

    transport.queue(remote, secondary_fragment_packet());
    stage.update(epoch + 2ms);
    CHECK(stage.state() ==
          goldsrc::InitialSignonState::secondary_stream_pending_m3);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::InitialSignonErrorCode::secondary_stream_pending_m3);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(stage.result() == std::nullopt);
}

TEST_CASE("Initial sign-on trace is metadata-only reentry-safe and exception-isolated",
          "[goldsrc][signon][stage][trace][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    std::vector<goldsrc::InitialSignonTraceEvent> trace;
    goldsrc::InitialSignonStage* stage_pointer = nullptr;
    goldsrc::InitialSignonStage stage{
        transport,
        remote,
        test_config(),
        [&](const goldsrc::InitialSignonTraceEvent& event) {
            trace.push_back(event);
            if (stage_pointer != nullptr) {
                stage_pointer->update(epoch + 1ms);
            }
            throw std::runtime_error{"synthetic trace exception"};
        }};
    stage_pointer = &stage;
    require_started(stage, transport, epoch);
    static_cast<void>(require_initial_request_sent(stage, transport, epoch + 2ms));

    REQUIRE(trace.size() >= 3U);
    CHECK(std::ranges::all_of(
        trace,
        [&](const goldsrc::InitialSignonTraceEvent& event) {
            return event.endpoint == remote && event.request_size == 5U &&
                   event.payload_size == 0U && !event.opcode.has_value();
        }));
    CHECK(stage.request_queue_count() == 1U);
    CHECK(transport.sent.size() == 1U);
}

} // namespace
