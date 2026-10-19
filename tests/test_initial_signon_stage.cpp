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
    config = test_config();
    config.maximum_pre_ack_payloads = 0U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
    config.maximum_pre_ack_payloads = goldsrc::kMaximumInitialPreAckPayloads;
    CHECK(goldsrc::valid_initial_signon_configuration(config));
    ++config.maximum_pre_ack_payloads;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
    config = test_config();
    config.maximum_pre_ack_payload_bytes = 0U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(config));
    config.maximum_pre_ack_payload_bytes = goldsrc::kMaximumInitialPreAckPayloadBytes;
    CHECK(goldsrc::valid_initial_signon_configuration(config));
    ++config.maximum_pre_ack_payload_bytes;
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

TEST_CASE("Initial sign-on pre-ACK FIFO obeys an explicit one-payload cap",
          "[goldsrc][signon][stage][payload][pre-ack][bounds]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_pre_ack_payloads = 1U;
    goldsrc::InitialSignonStage stage{transport, remote, config};
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

TEST_CASE("Initial sign-on decodes multiple pre-ACK payloads only after ACK and in order",
          "[goldsrc][signon][stage][payload][pre-ack][fifo]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    goldsrc::InitialSignonStage stage{transport, remote, test_config()};
    require_started(stage, transport, epoch);
    const auto sent = require_initial_request_sent(stage, transport, epoch + 1ms);
    transport.queue(remote, server_packet(1U, false, 0U, false,
        service_envelope(bytes({8U, 'a', 0U}))));
    transport.queue(remote, server_packet(2U, false, 0U, false,
        service_envelope(bytes({8U, 'b', 0U}))));
    transport.queue(remote, server_packet(3U, false, 0U, false,
        service_envelope(synthetic_boundary_payload())));
    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::waiting_for_request_ack);
    CHECK_FALSE(stage.error());
    CHECK_FALSE(stage.result());
    while (auto event = stage.poll_event()) {
        CHECK(event->type != goldsrc::InitialSignonEventType::service_message_decoded);
    }
    transport.queue(remote, server_packet(4U, false,
        sent.header.sequence.sequence.value(), true, {}));
    for (std::size_t step = 3U; step < 10U && !stage.terminal(); ++step) {
        stage.update(epoch + std::chrono::milliseconds(
            static_cast<std::chrono::milliseconds::rep>(step)));
    }
    REQUIRE(stage.result());
    CHECK_FALSE(stage.error());
    CHECK(stage.result()->service_payload_count == 3U);
    REQUIRE(stage.result()->messages.size() == 3U);
    CHECK(std::get<goldsrc::ServiceTextControl>(stage.result()->messages[0U].body).text == "a");
    CHECK(std::get<goldsrc::ServiceTextControl>(stage.result()->messages[1U].body).text == "b");
    CHECK(std::get<goldsrc::ServiceTextControl>(stage.result()->messages[2U].body).text ==
          "FAKE-HLDS EARLY TEXT");
    CHECK(stage.cleanup_count() == 1U);
    CHECK(stage.request_queue_count() == 1U);
}

TEST_CASE("Initial sign-on pre-ACK FIFO caps aggregate wire bytes before decoding",
          "[goldsrc][signon][stage][payload][pre-ack][bounds]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_pre_ack_payload_bytes = 3U;
    goldsrc::InitialSignonStage stage{transport, remote, config};
    require_started(stage, transport, epoch);
    static_cast<void>(require_initial_request_sent(stage, transport, epoch + 1ms));
    transport.queue(remote, server_packet(1U, false, 0U, false, bytes({1U, 1U})));
    stage.update(epoch + 2ms);
    CHECK_FALSE(stage.error());
    transport.queue(remote, server_packet(2U, false, 0U, false, bytes({1U, 1U})));
    stage.update(epoch + 3ms);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::InitialSignonErrorCode::service_payload_before_ack_overflow);
    CHECK_FALSE(stage.result());
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Initial sign-on ignores transport-only payloads without bypassing ACK",
          "[goldsrc][signon][stage][payload][pre-ack][empty]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_pre_ack_payloads = 1U;
    goldsrc::InitialSignonStage stage{transport, remote, config};
    require_started(stage, transport, epoch);
    const auto sent = require_initial_request_sent(stage, transport, epoch + 1ms);
    const auto empty_before = server_packet(1U, false, 0U, false, {});
    REQUIRE(empty_before.size() == 8U); // Truly header-only, no svc_nop padding.
    transport.queue(remote, empty_before);
    transport.queue(remote, server_packet(2U, false, 0U, false,
        service_envelope(synthetic_boundary_payload())));
    const auto empty_after = server_packet(3U, false, 0U, false, {});
    REQUIRE(empty_after.size() == 8U);
    transport.queue(remote, empty_after);
    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::InitialSignonState::waiting_for_request_ack);
    CHECK_FALSE(stage.error());
    CHECK_FALSE(stage.result());
    const auto acknowledge = server_packet(4U, false,
        sent.header.sequence.sequence.value(), true, {});
    REQUIRE(acknowledge.size() == 8U);
    transport.queue(remote, acknowledge);
    stage.update(epoch + 3ms);
    REQUIRE(stage.result());
    CHECK_FALSE(stage.error());
    CHECK(stage.result()->service_payload_count == 1U);
    CHECK(stage.result()->messages.size() == 1U);
    CHECK(stage.result()->boundary_payload.source_sequence == 2U);
    CHECK(stage.cleanup_count() == 1U);
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

TEST_CASE("Live initial sign-on accepts a bounded uncompressed service payload",
          "[goldsrc][signon][stage][uncompressed][live]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    auto config = test_config();
    config.service_payload_envelope.compression_policy =
        goldsrc::ServicePayloadCompressionPolicy::
            accept_bzip2_or_uncompressed;
    goldsrc::InitialSignonStage stage{transport, remote, config};
    require_started(stage, transport, epoch);
    static_cast<void>(stage.poll_event());
    const auto first =
        require_initial_request_sent(stage, transport, epoch + 1ms);
    static_cast<void>(stage.poll_event());

    const auto semantic = synthetic_boundary_payload();
    transport.queue(
        remote,
        server_packet(
            1U,
            false,
            first.header.sequence.sequence.value(),
            true,
            semantic));
    stage.update(epoch + 2ms);

    CAPTURE(static_cast<int>(stage.state()));
    const auto error_context =
        stage.error() ? stage.error()->context : std::string{};
    CAPTURE(error_context);
    REQUIRE(stage.result());
    CHECK(stage.state() ==
          goldsrc::InitialSignonState::signon_boundary_reached);
    CHECK(stage.result()->boundary_payload.bytes == semantic);
    CHECK_FALSE(stage.result()->boundary_payload.decompressed);
    CHECK(stage.result()->boundary_payload.wire_uncompressed);
    CHECK(stage.result()->service_payload_count == 1U);
    CHECK(stage.cleanup_count() == 1U);
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

TEST_CASE("Initial sign-on failures retain exact sanitized decoder provenance in trace",
          "[goldsrc][signon][stage][diagnostics][security][no-stock]")
{
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    const auto run = [&](std::vector<std::byte> semantic,
                         const std::optional<goldsrc::ServiceMessageErrorCode> expected_code,
                         const std::optional<std::uint8_t> expected_opcode,
                         const std::optional<std::size_t> expected_offset) {
        FakeTransport transport;
        std::vector<goldsrc::InitialSignonTraceEvent> traces;
        goldsrc::InitialSignonStage stage{transport, remote, test_config(),
            [&](const auto& event) { traces.push_back(event); }};
        require_started(stage, transport, epoch);
        const auto first = require_initial_request_sent(stage, transport, epoch + 1ms);
        transport.queue(remote, server_packet(7U, false,
            first.header.sequence.sequence.value(), true, service_envelope(semantic)));
        stage.update(epoch + 2ms);
        REQUIRE_FALSE(traces.empty());
        const auto failures = std::ranges::count_if(traces,
            [](const auto& event) { return event.service_message_code.has_value(); });
        if (!expected_code) {
            CHECK(failures == 0);
            CHECK_FALSE(stage.error());
            REQUIRE(stage.result());
            CHECK(std::ranges::all_of(traces, [](const auto& event) {
                return !event.wire_opcode && !event.service_error_byte_offset && !event.source_sequence;
            }));
            return;
        }
        CHECK(failures == 1);
        REQUIRE(stage.error());
        CHECK(stage.error()->service_message_code == expected_code);
        CHECK(stage.error()->wire_opcode == expected_opcode);
        CHECK(stage.error()->service_error_byte_offset == expected_offset);
        CHECK(stage.error()->source_sequence == 7U);
        CHECK(stage.error()->context.size() <= goldsrc::kInitialSignonDiagnosticTextLimit);
        CHECK(stage.error()->context.find("FAKE-HLDS") == std::string::npos);
        const auto& event = traces.back();
        CHECK(event.classification == goldsrc::InitialSignonTraceClassification::protocol_failure);
        CHECK(event.service_message_code == expected_code);
        CHECK(event.wire_opcode == expected_opcode);
        CHECK(event.service_error_byte_offset == expected_offset);
        CHECK(event.source_sequence == 7U);
    };
    run(bytes({8U, 65U, 0U, 127U, 1U}),
        goldsrc::ServiceMessageErrorCode::unsupported_service_opcode, 127U, 3U);
    run(bytes({8U, 65U}), goldsrc::ServiceMessageErrorCode::unterminated_string, 8U, 2U);
    run(bytes({11U}), goldsrc::ServiceMessageErrorCode::boundary_body_missing, 11U, 1U);
    run(synthetic_boundary_payload(), std::nullopt, std::nullopt, std::nullopt);
}

TEST_CASE("Decoded sign-on batch event backpressure commits no partial semantics",
          "[goldsrc][signon][stage][backpressure][batch][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128U);
    const auto epoch = goldsrc::InitialSignonTimePoint{} + 1s;
    auto config = test_config();
    // This batch needs five atomic publications even with an EMPTY ring:
    // payload, three simple messages, boundary. It cannot become a deferred
    // success merely by draining the earlier queued/transmitted/ACK metadata.
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
            service_envelope(bytes({1U,1U,1U,11U,48U}))));
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

TEST_CASE("Initial sign-on accepts director at byte zero of the first live service carrier",
          "[goldsrc][signon][stage][director-signon][live]")
{
    FakeTransport transport;
    const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    goldsrc::InitialSignonStage stage{transport,remote,config};
    require_started(stage,transport,epoch);
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    auto semantic=bytes({51U,6U,11U,14U,43U,9U,57U,58U});
    const auto tail=synthetic_boundary_payload();semantic.insert(semantic.end(),tail.begin(),tail.end());
    transport.queue(remote,server_packet(1U,false,sent.header.sequence.sequence.value(),true,semantic));
    stage.update(epoch+2ms);
    REQUIRE(stage.result());CHECK_FALSE(stage.error());
    CHECK(stage.state()==goldsrc::InitialSignonState::signon_boundary_reached);
    CHECK(stage.result()->service_payload_count==1U);
    REQUIRE(stage.result()->messages.size()==2U);
    const auto& director=stage.result()->messages[0];
    CHECK(static_cast<unsigned>(director.opcode)==51U);
    CHECK(director.kind==goldsrc::ServiceMessageKind::ignored_director);
    CHECK(std::get<goldsrc::ServiceIgnoredDirector>(director.body).body_size==6U);
    CHECK(director.byte_offset==0U);CHECK(director.byte_count==8U);
    CHECK(stage.result()->messages[1].byte_offset==8U);
    CHECK(stage.result()->boundary.byte_offset==30U);
    CHECK(stage.result()->boundary.remaining_byte_count==4U);
    CHECK(stage.result()->boundary_payload.bytes==semantic);
    CHECK(stage.result()->boundary_payload.source_sequence==1U);
    CHECK(stage.result()->boundary_payload.source_acknowledgement==sent.header.sequence.sequence.value());
    CHECK(stage.result()->boundary_payload.acknowledgement_reliable);
    CHECK(stage.result()->boundary_payload.wire_uncompressed);
    CHECK_FALSE(stage.result()->boundary_payload.decompressed);
    CHECK(stage.result()->boundary_payload.received_at==epoch+2ms);
    CHECK(stage.request_queue_count()==1U);CHECK(stage.cleanup_count()==1U);
}

TEST_CASE("Director-only early carriers wait for the exact ACK and preserve ordered boundary provenance",
          "[goldsrc][signon][stage][director-signon][pre-ack][fifo]")
{
    FakeTransport transport;
    const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    goldsrc::InitialSignonStage stage{transport,remote,config};
    require_started(stage,transport,epoch);
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    const auto first=server_packet(1U,false,0U,false,bytes({51U,0U}));
    transport.queue(remote,first);stage.update(epoch+2ms);
    CHECK(stage.state()==goldsrc::InitialSignonState::waiting_for_request_ack);
    CHECK_FALSE(stage.result());CHECK_FALSE(stage.error());
    while(const auto event=stage.poll_event())
        CHECK(event->type!=goldsrc::InitialSignonEventType::service_message_decoded);

    transport.queue(remote,first); // A retransmitted receive carrier is not a second Director event.
    transport.queue(remote,server_packet(2U,false,0U,false,bytes({51U,1U,11U})));
    const auto boundary=synthetic_boundary_payload();
    transport.queue(remote,server_packet(3U,false,0U,false,boundary));
    stage.update(epoch+3ms);
    CHECK(stage.state()==goldsrc::InitialSignonState::waiting_for_request_ack);
    CHECK_FALSE(stage.result());CHECK_FALSE(stage.error());
    while(const auto event=stage.poll_event())
        CHECK(event->type!=goldsrc::InitialSignonEventType::service_message_decoded);
    transport.queue(remote,server_packet(4U,false,sent.header.sequence.sequence.value(),true,{}));
    for(unsigned step=4U;step<12U&&!stage.terminal();++step)
        stage.update(epoch+std::chrono::milliseconds{step});
    REQUIRE(stage.result());CHECK_FALSE(stage.error());
    REQUIRE(stage.result()->messages.size()==3U);
    CHECK(stage.result()->service_payload_count==3U);
    CHECK(std::get<goldsrc::ServiceIgnoredDirector>(stage.result()->messages[0].body).body_size==0U);
    CHECK(std::get<goldsrc::ServiceIgnoredDirector>(stage.result()->messages[1].body).body_size==1U);
    CHECK(std::get<goldsrc::ServiceTextControl>(stage.result()->messages[2].body).text=="FAKE-HLDS EARLY TEXT");
    const auto& source=stage.result()->boundary_payload;
    CHECK(source.source_sequence==3U);CHECK(source.source_acknowledgement==0U);
    CHECK_FALSE(source.acknowledgement_reliable);CHECK_FALSE(source.source_reliable);
    CHECK(source.received_at==epoch+3ms);CHECK(source.bytes==boundary);
    CHECK(source.wire_uncompressed);CHECK_FALSE(source.reassembled);
    std::vector<goldsrc::InitialSignonEvent> decoded;
    std::size_t acknowledgements{},boundaries{};
    while(const auto event=stage.poll_event()) {
        if(event->type==goldsrc::InitialSignonEventType::initial_request_acknowledged)++acknowledgements;
        if(event->type==goldsrc::InitialSignonEventType::signon_boundary_reached)++boundaries;
        if(event->type==goldsrc::InitialSignonEventType::service_message_decoded)decoded.push_back(*event);
    }
    REQUIRE(decoded.size()==3U);CHECK(acknowledgements==1U);CHECK(boundaries==1U);
    CHECK(decoded[0].opcode==goldsrc::ServiceMessageOpcode::director);CHECK(decoded[0].byte_count==2U);
    CHECK(decoded[1].opcode==goldsrc::ServiceMessageOpcode::director);CHECK(decoded[1].byte_count==3U);
    CHECK(decoded[2].opcode==goldsrc::ServiceMessageOpcode::text_control);
    const auto sends=transport.sent.size();
    transport.queue(remote,first);stage.update(epoch+12ms);
    CHECK_FALSE(stage.poll_event());CHECK(transport.sent.size()==sends);
    CHECK(stage.request_queue_count()==1U);CHECK(stage.cleanup_count()==1U);
}

TEST_CASE("Initial director failures preserve exact source diagnostics and atomic publication",
          "[goldsrc][signon][stage][director-signon][security][diagnostics]")
{
    FakeTransport transport;
    const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    auto semantic=bytes({51U});
    auto expected=goldsrc::ServiceMessageErrorCode::truncated_control_body;
    std::uint8_t opcode=51U;std::size_t offset=1U;
    SECTION("A missing length does not publish a Director event"){}
    SECTION("An incomplete body does not reinterpret a fake boundary"){
        semantic=bytes({51U,4U,11U,48U});offset=semantic.size();
    }
    SECTION("An actual unsupported next opcode is not skipped"){
        semantic=bytes({51U,0U,254U,11U,48U});expected=goldsrc::ServiceMessageErrorCode::unsupported_service_opcode;
        opcode=254U;offset=2U;
    }
    std::vector<goldsrc::InitialSignonTraceEvent> traces;
    goldsrc::InitialSignonStage stage{transport,remote,config,[&](const auto& event){traces.push_back(event);}};
    require_started(stage,transport,epoch);
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    transport.queue(remote,server_packet(1U,false,sent.header.sequence.sequence.value(),true,semantic));
    stage.update(epoch+2ms);
    REQUIRE(stage.error());CHECK_FALSE(stage.result());
    CHECK(stage.error()->service_message_code==expected);
    CHECK(stage.error()->wire_opcode==opcode);CHECK(stage.error()->service_error_byte_offset==offset);
    CHECK(stage.error()->source_sequence==1U);
    REQUIRE_FALSE(traces.empty());CHECK(traces.back().source_sequence==1U);
    CHECK(traces.back().wire_opcode==opcode);CHECK(traces.back().service_error_byte_offset==offset);
    while(const auto event=stage.poll_event()) {
        CHECK(event->type!=goldsrc::InitialSignonEventType::service_message_decoded);
        CHECK(event->type!=goldsrc::InitialSignonEventType::signon_boundary_reached);
    }
    CHECK(stage.request_queue_count()==1U);CHECK(stage.cleanup_count()==1U);
}

TEST_CASE("Initial director metadata cannot evade FIFO or event publication budgets",
          "[goldsrc][signon][stage][director-signon][limits]")
{
    FakeTransport transport;
    const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    bool ack=false;
    SECTION("Pre-ACK Director count is capped"){config.maximum_pre_ack_payloads=1U;}
    SECTION("Pre-ACK Director bytes are capped"){config.maximum_pre_ack_payload_bytes=3U;}
    SECTION("Decoded Director and boundary publish atomically"){config.maximum_events=4U;ack=true;}
    goldsrc::InitialSignonStage stage{transport,remote,config};
    require_started(stage,transport,epoch);
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    if(ack)transport.queue(remote,server_packet(1U,false,sent.header.sequence.sequence.value(),true,
        bytes({51U,0U,51U,0U,51U,0U,11U,48U})));
    else {
        transport.queue(remote,server_packet(1U,false,0U,false,bytes({51U,0U})));
        transport.queue(remote,server_packet(2U,false,0U,false,bytes({51U,0U})));
    }
    stage.update(epoch+2ms);
    REQUIRE(stage.error());CHECK_FALSE(stage.result());
    CHECK(stage.error()->code==(ack?goldsrc::InitialSignonErrorCode::service_event_backpressure:
        goldsrc::InitialSignonErrorCode::service_payload_before_ack_overflow));
    while(const auto event=stage.poll_event()) {
        CHECK(event->type!=goldsrc::InitialSignonEventType::service_message_decoded);
        CHECK(event->type!=goldsrc::InitialSignonEventType::signon_boundary_reached);
    }
    CHECK(stage.request_queue_count()==1U);CHECK(stage.cleanup_count()==1U);
}

[[nodiscard]] std::vector<std::byte> capacity_directors(const std::size_t count,
    const std::uint8_t body_size=0U)
{
    std::vector<std::byte> result;
    for(std::size_t i=0;i<count;++i) {
        result.push_back(std::byte{51U});result.push_back(static_cast<std::byte>(body_size));
        result.insert(result.end(),body_size,std::byte{11U});
    }
    return result;
}

TEST_CASE("Normal initial sign-on admits a large valid first carrier within independent bounds",
          "[goldsrc][signon][stage][signon-capacity]")
{
    std::size_t director_count=40U;
    SECTION("Forty controls exceed the former event and retained-state defaults"){}
    SECTION("Sixty-two controls plus text and boundary exactly fill the decoder default"){
        director_count=62U;
    }
    FakeTransport transport;const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();config.maximum_events=goldsrc::InitialSignonConfig{}.maximum_events;
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    goldsrc::InitialSignonStage stage{transport,remote,config};
    require_started(stage,transport,epoch);
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    auto payload=capacity_directors(director_count);
    const auto boundary=synthetic_boundary_payload();payload.insert(payload.end(),boundary.begin(),boundary.end());
    transport.queue(remote,server_packet(1U,false,sent.header.sequence.sequence.value(),true,payload));
    stage.update(epoch+2ms);
    REQUIRE(stage.result());CHECK_FALSE(stage.error());
    REQUIRE(stage.result()->messages.size()==director_count+1U);CHECK(stage.result()->service_payload_count==1U);
    for(std::size_t i=0;i<director_count;++i) {
        const auto& message=stage.result()->messages[i];
        CHECK(message.opcode==goldsrc::ServiceMessageOpcode::director);
        CHECK(message.byte_offset==i*2U);CHECK(message.byte_count==2U);
        CHECK(std::get<goldsrc::ServiceIgnoredDirector>(message.body).body_size==0U);
    }
    CHECK(stage.result()->messages.back().byte_offset==director_count*2U);
    CHECK(stage.result()->boundary.byte_offset==director_count*2U+22U);CHECK(stage.result()->boundary.remaining_byte_count==4U);
    CHECK(stage.result()->boundary_payload.bytes==payload);
    CHECK(stage.result()->boundary_payload.source_sequence==1U);
    CHECK(stage.result()->boundary_payload.received_at==epoch+2ms);
    std::size_t events{},messages{},acknowledgements{};
    while(const auto event=stage.poll_event()) {
        ++events;
        if(event->type==goldsrc::InitialSignonEventType::service_message_decoded)++messages;
        if(event->type==goldsrc::InitialSignonEventType::initial_request_acknowledged)++acknowledgements;
    }
    CHECK(events==director_count+6U);CHECK(messages==director_count+1U);CHECK(acknowledgements==1U);
    CHECK(stage.request_queue_count()==1U);CHECK(stage.cleanup_count()==1U);
}

TEST_CASE("Retained initial messages are independent of a drained event ring",
          "[goldsrc][signon][stage][signon-capacity]")
{
    FakeTransport transport;const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();config.maximum_events=16U;
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    goldsrc::InitialSignonStage stage{transport,remote,config};
    require_started(stage,transport,epoch);
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    std::size_t messages{},payloads{};
    const auto drain=[&] {
        while(const auto event=stage.poll_event()) {
            if(event->type==goldsrc::InitialSignonEventType::service_message_decoded)++messages;
            if(event->type==goldsrc::InitialSignonEventType::service_payload_received)++payloads;
        }
    };
    drain();
    for(std::uint32_t sequence_number=1U;sequence_number<=3U;++sequence_number) {
        transport.queue(remote,server_packet(sequence_number,false,sent.header.sequence.sequence.value(),true,
            capacity_directors(12U,static_cast<std::uint8_t>(sequence_number-1U))));
        stage.update(epoch+std::chrono::milliseconds{sequence_number+1U});
        CHECK_FALSE(stage.error());CHECK_FALSE(stage.result());drain();
        CHECK(messages==sequence_number*12U);CHECK(payloads==sequence_number);
    }
    transport.queue(remote,server_packet(4U,false,sent.header.sequence.sequence.value(),true,
        synthetic_boundary_payload()));
    stage.update(epoch+5ms);drain();
    REQUIRE(stage.result());CHECK_FALSE(stage.error());
    REQUIRE(stage.result()->messages.size()==37U);CHECK(messages==37U);CHECK(payloads==4U);
    for(std::size_t i=0;i<36U;++i) {
        const auto& message=stage.result()->messages[i];
        CHECK(std::get<goldsrc::ServiceIgnoredDirector>(message.body).body_size==i/12U);
        CHECK(message.byte_offset==(i%12U)*(2U+i/12U));
    }
    CHECK(stage.result()->boundary_payload.source_sequence==4U);
    CHECK(stage.result()->boundary_payload.received_at==epoch+5ms);
    CHECK(stage.request_queue_count()==1U);CHECK(stage.cleanup_count()==1U);
}

TEST_CASE("A full initial event ring yields one whole decoded batch until its consumer drains",
          "[goldsrc][signon][stage][signon-capacity][pre-ack][fifo]")
{
    FakeTransport transport;const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();config.maximum_events=16U;
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    std::size_t releases{};
    goldsrc::InitialSignonStage stage{transport,remote,config};
    require_started(stage,transport,epoch,std::make_unique<CountingLifetime>(releases));
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    for(std::uint32_t sequence_number=1U;sequence_number<=3U;++sequence_number) {
        const auto carrier=server_packet(sequence_number,false,0U,false,
            capacity_directors(12U,static_cast<std::uint8_t>(sequence_number-1U)));
        transport.queue(remote,carrier);
        if(sequence_number==1U)transport.queue(remote,carrier); // Duplicate cannot consume a second FIFO slot.
    }
    const auto final_payload=synthetic_boundary_payload();
    transport.queue(remote,server_packet(4U,false,0U,false,final_payload));
    stage.update(epoch+2ms);CHECK_FALSE(stage.error());CHECK_FALSE(stage.result());
    CHECK(stage.pending_event_count()==2U);
    transport.queue(remote,server_packet(5U,false,sent.header.sequence.sequence.value(),true,{}));
    stage.update(epoch+3ms);
    CHECK_FALSE(stage.error());CHECK_FALSE(stage.result());CHECK_FALSE(stage.terminal());
    CHECK(stage.pending_event_count()==16U);CHECK(releases==0U);
    const auto receive_calls=transport.receive_calls,sends=transport.sent.size();
    stage.update(epoch+4ms); // No consumer progress: no decode commit, packet read or resend.
    CHECK_FALSE(stage.error());CHECK_FALSE(stage.result());CHECK(stage.pending_event_count()==16U);
    CHECK(transport.receive_calls==receive_calls);CHECK(transport.sent.size()==sends);CHECK(releases==0U);

    std::vector<goldsrc::InitialSignonEvent> messages;
    std::size_t payloads{},acknowledgements{},boundaries{};
    const auto drain=[&] {
        while(const auto event=stage.poll_event()) {
            if(event->type==goldsrc::InitialSignonEventType::service_message_decoded)messages.push_back(*event);
            if(event->type==goldsrc::InitialSignonEventType::service_payload_received)++payloads;
            if(event->type==goldsrc::InitialSignonEventType::initial_request_acknowledged)++acknowledgements;
            if(event->type==goldsrc::InitialSignonEventType::signon_boundary_reached)++boundaries;
        }
    };
    drain();CHECK(messages.size()==12U);CHECK(payloads==1U);CHECK(acknowledgements==1U);
    for(unsigned step=5U;step<14U&&!stage.terminal();++step) {
        stage.update(epoch+std::chrono::milliseconds{step});drain();
    }
    REQUIRE(stage.result());CHECK_FALSE(stage.error());
    REQUIRE(stage.result()->messages.size()==37U);REQUIRE(messages.size()==37U);
    CHECK(payloads==4U);CHECK(acknowledgements==1U);CHECK(boundaries==1U);
    CHECK(stage.result()->service_payload_count==4U);
    for(std::size_t i=0;i<36U;++i) {
        CHECK(std::get<goldsrc::ServiceIgnoredDirector>(stage.result()->messages[i].body).body_size==i/12U);
        CHECK(messages[i].byte_offset==(i%12U)*(2U+i/12U));
        CHECK(messages[i].byte_count==2U+i/12U);
    }
    const auto& source=stage.result()->boundary_payload;
    CHECK(source.bytes==final_payload);CHECK(source.source_sequence==4U);
    CHECK(source.source_acknowledgement==0U);CHECK_FALSE(source.acknowledgement_reliable);
    CHECK(source.received_at==epoch+2ms); // Deferred commit must not replace original receive time.
    CHECK(stage.request_queue_count()==1U);CHECK(stage.cleanup_count()==1U);CHECK(releases==1U);
    const auto terminal_sends=transport.sent.size();stage.update(epoch+15ms);
    CHECK_FALSE(stage.poll_event());CHECK(transport.sent.size()==terminal_sends);
}

TEST_CASE("Independent retained-message capacity is validated and rejects only the overflowing batch",
          "[goldsrc][signon][stage][signon-capacity][limits]")
{
    auto invalid=test_config();invalid.maximum_retained_messages=0U;
    CHECK_FALSE(goldsrc::valid_initial_signon_configuration(invalid));
    invalid.maximum_retained_messages=256U;CHECK(goldsrc::valid_initial_signon_configuration(invalid));
    invalid.maximum_retained_messages=257U;CHECK_FALSE(goldsrc::valid_initial_signon_configuration(invalid));
    FakeTransport transport;const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();config.maximum_retained_messages=20U;
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    std::vector<goldsrc::InitialSignonTraceEvent> traces;
    goldsrc::InitialSignonStage stage{transport,remote,config,[&](const auto& event){traces.push_back(event);}};
    require_started(stage,transport,epoch);const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    transport.queue(remote,server_packet(1U,false,sent.header.sequence.sequence.value(),true,capacity_directors(20U)));
    stage.update(epoch+2ms);CHECK_FALSE(stage.error());CHECK_FALSE(stage.result());
    std::size_t committed{};
    while(const auto event=stage.poll_event())if(event->type==goldsrc::InitialSignonEventType::service_message_decoded)++committed;
    CHECK(committed==20U);
    transport.queue(remote,server_packet(2U,false,sent.header.sequence.sequence.value(),true,
        bytes({51U,0U,11U,48U})));
    stage.update(epoch+3ms);
    REQUIRE(stage.error());CHECK(stage.error()->code==goldsrc::InitialSignonErrorCode::service_event_backpressure);
    CHECK(stage.error()->wire_opcode==51U);CHECK(stage.error()->service_error_byte_offset==0U);
    CHECK(stage.error()->source_sequence==2U);CHECK_FALSE(stage.error()->service_message_code);
    REQUIRE_FALSE(traces.empty());
    const auto& failure=traces.back();
    CHECK(failure.classification==goldsrc::InitialSignonTraceClassification::backpressure);
    CHECK(failure.stage_error_code==goldsrc::InitialSignonErrorCode::service_event_backpressure);
    CHECK(failure.wire_opcode==51U);CHECK(failure.service_error_byte_offset==0U);
    CHECK(failure.source_sequence==2U);CHECK_FALSE(failure.service_message_code);
    CHECK_FALSE(stage.result());
    while(const auto event=stage.poll_event()) {
        CHECK(event->type!=goldsrc::InitialSignonEventType::service_message_decoded);
        CHECK(event->type!=goldsrc::InitialSignonEventType::signon_boundary_reached);
    }
    CHECK(stage.cleanup_count()==1U);CHECK(stage.request_queue_count()==1U);
}

TEST_CASE("Cancelling or reversing time releases a deferred initial batch without publishing it",
          "[goldsrc][signon][stage][signon-capacity][lifetime]")
{
    FakeTransport transport;const auto remote=network::NetworkAddress::loopback(27'128U);
    const auto epoch=goldsrc::InitialSignonTimePoint{}+1s;
    auto config=test_config();config.maximum_events=4U;
    config.service_payload_envelope.compression_policy=
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
    bool backwards=false;
    SECTION("Explicit cancellation releases the retained decoded owner"){}
    SECTION("Backwards owner time releases the retained decoded owner"){backwards=true;}
    std::size_t releases{};
    goldsrc::InitialSignonStage stage{transport,remote,config};
    require_started(stage,transport,epoch,std::make_unique<CountingLifetime>(releases));
    const auto sent=require_initial_request_sent(stage,transport,epoch+1ms);
    transport.queue(remote,server_packet(1U,false,sent.header.sequence.sequence.value(),true,
        bytes({51U,0U,11U,48U})));
    stage.update(epoch+2ms);
    CHECK_FALSE(stage.error());CHECK_FALSE(stage.result());CHECK_FALSE(stage.terminal());
    CHECK(stage.pending_event_count()==3U);CHECK(releases==0U);
    if(backwards)stage.update(epoch+1ms);else stage.cancel(epoch+3ms);
    CHECK(stage.terminal());CHECK_FALSE(stage.result());CHECK(stage.cleanup_count()==1U);CHECK(releases==1U);
    if(backwards){REQUIRE(stage.error());CHECK(stage.error()->code==goldsrc::InitialSignonErrorCode::time_moved_backwards);}
    else CHECK(stage.state()==goldsrc::InitialSignonState::cancelled);
    while(const auto event=stage.poll_event()) {
        CHECK(event->type!=goldsrc::InitialSignonEventType::service_message_decoded);
        CHECK(event->type!=goldsrc::InitialSignonEventType::signon_boundary_reached);
    }
    const auto sends=transport.sent.size();stage.update(epoch+4ms);stage.cancel(epoch+5ms);
    CHECK(transport.sent.size()==sends);CHECK_FALSE(stage.poll_event());CHECK(releases==1U);
}

} // namespace
