#include <hlclient/goldsrc/netchan_driver.hpp>

#include <catch2/catch_test_macros.hpp>

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
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

static_assert(!std::is_copy_constructible_v<goldsrc::NetchanDriver>);
static_assert(!std::is_move_constructible_v<goldsrc::NetchanDriver>);
static_assert(!std::is_copy_constructible_v<goldsrc::INetchanDriverLifetime>);

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
        if (throw_on_local_address) {
            throw std::runtime_error{"synthetic local endpoint exception"};
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
        if (!send_results.empty()) {
            auto result = std::move(send_results.front());
            send_results.pop_front();
            if (!result) {
                return result;
            }
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
        const auto size = payload.size();
        incoming.push_back(network::DatagramTransportReceiveResult{
            network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(payload)},
            source,
            size,
            {},
        });
    }

    void queue_receive_error()
    {
        incoming.push_back(network::DatagramTransportReceiveResult{
            network::DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic receive failure",
        });
    }

    std::optional<network::NetworkAddress> local{
        network::NetworkAddress::loopback(31'000U)};
    std::string local_error;
    mutable bool throw_on_local_address{false};
    bool throw_on_receive{false};
    bool throw_on_send{false};
    mutable std::size_t local_queries{0U};
    std::size_t receive_calls{0U};
    std::size_t send_attempts{0U};
    std::vector<std::size_t> receive_limits;
    std::deque<network::DatagramTransportReceiveResult> incoming;
    std::deque<network::DatagramSendResult> send_results;
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

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    std::vector<std::byte> output;
    output.reserve(text.size());
    std::ranges::transform(text, std::back_inserter(output), [](const char value) {
        return std::byte{static_cast<std::uint8_t>(value)};
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
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"unable to encode synthetic server packet"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> server_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    std::vector<std::byte> fragment_payload,
    std::vector<std::byte> ordinary_suffix = {},
    const std::uint8_t slot = 0U)
{
    if (slot >= goldsrc::kNetchanFragmentSlotCount || fragment_payload.empty() ||
        fragment_payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error{"invalid synthetic fragment"};
    }
    const auto fragment_size = fragment_payload.size();
    fragment_payload.insert(
        fragment_payload.end(), ordinary_suffix.begin(), ordinary_suffix.end());
    goldsrc::NetchanFragmentSlots fragments;
    fragments[slot] = goldsrc::NetchanFragmentDescriptor{
        slot,
        (static_cast<std::uint32_t>(fragment_index) << 16U) |
            static_cast<std::uint32_t>(fragment_count),
        0U,
        static_cast<std::uint16_t>(fragment_size),
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{true, true},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                reliable_acknowledgement,
            },
        },
        std::move(fragments),
        std::move(fragment_payload),
        fragment_size,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"unable to encode synthetic server fragment"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> canonical_payload(const std::size_t size)
{
    std::vector<std::byte> output(size);
    for (std::size_t index = 0U; index < output.size(); ++index) {
        output[index] = std::byte{static_cast<std::uint8_t>(index % 251U)};
    }
    return output;
}

[[nodiscard]] std::vector<std::byte> canonical_range(
    const std::vector<std::byte>& payload,
    const std::size_t offset,
    const std::size_t length)
{
    return std::vector<std::byte>{
        payload.begin() + static_cast<std::ptrdiff_t>(offset),
        payload.begin() + static_cast<std::ptrdiff_t>(offset + length)};
}

[[nodiscard]] goldsrc::NetchanDriverConfig test_config()
{
    goldsrc::NetchanDriverConfig config;
    config.channel_inactivity_timeout = 100ms;
    config.fragment_transfer_timeout = 50ms;
    config.maximum_datagram_size = goldsrc::kDefaultNetchanDatagramSize;
    config.maximum_fragment_datagram_size =
        goldsrc::kDefaultNetchanFragmentDatagramSize;
    config.maximum_fragment_payload_size =
        goldsrc::kDefaultMaximumNetchanFragmentPayloadSize;
    config.maximum_normal_transfer_size =
        goldsrc::kDefaultMaximumNetchanNormalTransferSize;
    config.maximum_fragments_per_transfer =
        goldsrc::kDefaultMaximumNetchanFragmentsPerTransfer;
    config.maximum_active_normal_transfers =
        goldsrc::kDefaultMaximumActiveNormalTransfers;
    config.maximum_fragment_ranges =
        goldsrc::kDefaultMaximumNetchanFragmentRanges;
    config.maximum_opaque_payload_size =
        goldsrc::kDefaultNetchanDriverOpaquePayloadSize;
    config.maximum_unreliable_payload_size =
        config.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    config.maximum_datagrams_per_update = 8U;
    config.maximum_outgoing_packets_per_update = 1U;
    config.maximum_events = 16U;
    return config;
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

[[nodiscard]] std::vector<std::byte> exact_first_acknowledgement_bytes()
{
    std::vector<std::byte> output;
    output.reserve(exact_first_acknowledgement().size());
    std::ranges::transform(
        exact_first_acknowledgement(),
        std::back_inserter(output),
        [](const std::uint8_t value) { return std::byte{value}; });
    return output;
}

void require_started(
    goldsrc::NetchanDriver& driver,
    FakeTransport& transport,
    const goldsrc::NetchanDriverTimePoint now)
{
    REQUIRE(transport.local);
    REQUIRE(driver.start(now, *transport.local));
    CHECK(driver.state() == goldsrc::NetchanDriverState::active);
    CHECK_FALSE(driver.terminal());
    CHECK(driver.local_endpoint() == transport.local);
    CHECK(transport.sent.empty());
}

} // namespace

TEST_CASE("Netchan driver configuration has explicit hard limits",
          "[goldsrc][netchan][driver][limits]")
{
    CHECK(goldsrc::valid_configuration(test_config()));

    auto at_limit = test_config();
    at_limit.channel_inactivity_timeout =
        goldsrc::kMaximumNetchanChannelInactivityTimeout;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.fragment_transfer_timeout =
        goldsrc::kMaximumNetchanFragmentTransferTimeout;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_datagram_size = goldsrc::kMaximumNetchanDatagramSize;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_unfragmented_reliable_payload = 5U;
    at_limit.maximum_pending_reliable_payload = 5U;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_unfragmented_reliable_payload =
        at_limit.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    at_limit.maximum_pending_reliable_payload =
        goldsrc::kMaximumPendingReliablePayload;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_datagram_size = goldsrc::kMaximumNetchanDatagramSize;
    at_limit.maximum_fragment_datagram_size =
        goldsrc::kMaximumNetchanFragmentDatagramSize;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_fragment_payload_size =
        goldsrc::kMaximumNetchanFragmentPayloadSize;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_normal_transfer_size =
        goldsrc::kMaximumNetchanNormalTransferSize;
    at_limit.maximum_fragments_per_transfer =
        goldsrc::kMaximumNetchanFragmentsPerTransfer;
    at_limit.maximum_fragment_ranges =
        goldsrc::kMaximumNetchanFragmentRanges;
    at_limit.maximum_opaque_payload_size =
        goldsrc::kMaximumNetchanDriverOpaquePayloadSize;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_fragments_per_transfer =
        goldsrc::kMaximumNetchanFragmentsPerTransfer;
    at_limit.maximum_fragment_ranges =
        goldsrc::kMaximumNetchanFragmentRanges;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_fragment_ranges =
        goldsrc::kMaximumNetchanFragmentRanges;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_events = goldsrc::kMaximumNetchanDriverEvents;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_datagrams_per_update =
        goldsrc::kMaximumNetchanDriverDatagramsPerUpdate;
    CHECK(goldsrc::valid_configuration(at_limit));
    at_limit = test_config();
    at_limit.maximum_outgoing_packets_per_update =
        goldsrc::kMaximumNetchanDriverOutgoingPacketsPerUpdate;
    CHECK(goldsrc::valid_configuration(at_limit));

    const auto invalid = [](goldsrc::NetchanDriverConfig config) {
        CHECK_FALSE(goldsrc::valid_configuration(config));
    };
    auto config = test_config();
    config.channel_inactivity_timeout = 0ms;
    invalid(config);
    config = test_config();
    config.channel_inactivity_timeout =
        goldsrc::kMaximumNetchanChannelInactivityTimeout + 1ms;
    invalid(config);
    config = test_config();
    config.fragment_transfer_timeout = 0ms;
    invalid(config);
    config = test_config();
    config.fragment_transfer_timeout =
        goldsrc::kMaximumNetchanFragmentTransferTimeout + 1ms;
    invalid(config);
    config = test_config();
    config.maximum_datagram_size =
        goldsrc::kNetchanHeaderSize +
        goldsrc::kStockProtocol48MinimumDecodedPayloadSize - 1U;
    invalid(config);
    config = test_config();
    config.maximum_datagram_size = goldsrc::kMaximumNetchanDatagramSize + 1U;
    invalid(config);
    config = test_config();
    config.maximum_unfragmented_reliable_payload = 0U;
    invalid(config);
    config = test_config();
    config.maximum_unfragmented_reliable_payload =
        config.maximum_datagram_size - goldsrc::kNetchanHeaderSize + 1U;
    invalid(config);
    config = test_config();
    config.maximum_pending_reliable_payload = 0U;
    invalid(config);
    config = test_config();
    config.maximum_pending_reliable_payload =
        goldsrc::kMaximumPendingReliablePayload + 1U;
    invalid(config);
    config = test_config();
    config.maximum_unfragmented_reliable_payload = 5U;
    config.maximum_pending_reliable_payload = 4U;
    invalid(config);
    config = test_config();
    config.maximum_fragment_datagram_size =
        goldsrc::kNetchanHeaderSize +
        goldsrc::kStockProtocol48PresentFragmentDescriptorSize +
        goldsrc::kStockProtocol48FragmentPresenceSize;
    invalid(config);
    config = test_config();
    config.maximum_fragment_datagram_size =
        goldsrc::kMaximumNetchanFragmentDatagramSize + 1U;
    config.maximum_datagram_size = goldsrc::kMaximumNetchanDatagramSize;
    invalid(config);
    config = test_config();
    config.maximum_fragment_payload_size = 0U;
    invalid(config);
    config = test_config();
    config.maximum_fragment_payload_size =
        goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U;
    invalid(config);
    config = test_config();
    config.maximum_fragment_payload_size =
        goldsrc::kMaximumNetchanFragmentPayloadSize + 1U;
    invalid(config);
    config = test_config();
    config.maximum_normal_transfer_size = 0U;
    invalid(config);
    config = test_config();
    config.maximum_normal_transfer_size =
        goldsrc::kMaximumNetchanNormalTransferSize + 1U;
    invalid(config);
    config = test_config();
    config.maximum_fragments_per_transfer = 0U;
    invalid(config);
    config = test_config();
    config.maximum_fragments_per_transfer =
        goldsrc::kMaximumNetchanFragmentsPerTransfer + 1U;
    config.maximum_fragment_ranges =
        goldsrc::kMaximumNetchanFragmentRanges;
    invalid(config);
    config = test_config();
    config.maximum_active_normal_transfers = 0U;
    invalid(config);
    config = test_config();
    config.maximum_active_normal_transfers =
        goldsrc::kMaximumActiveNormalTransfers + 1U;
    invalid(config);
    config = test_config();
    config.maximum_fragment_ranges = 0U;
    invalid(config);
    config = test_config();
    config.maximum_fragment_ranges =
        goldsrc::kMaximumNetchanFragmentRanges + 1U;
    invalid(config);
    config = test_config();
    config.maximum_fragment_ranges =
        config.maximum_fragments_per_transfer - 1U;
    invalid(config);
    config = test_config();
    config.maximum_fragments_per_transfer = 1U;
    config.maximum_fragment_ranges = 1U;
    config.maximum_normal_transfer_size =
        goldsrc::kStockProtocol48NormalFragmentChunkSize + 1U;
    config.maximum_opaque_payload_size =
        config.maximum_normal_transfer_size;
    invalid(config);
    config = test_config();
    config.maximum_opaque_payload_size = 0U;
    invalid(config);
    config = test_config();
    config.maximum_opaque_payload_size =
        goldsrc::kMaximumNetchanDriverOpaquePayloadSize + 1U;
    invalid(config);
    config = test_config();
    config.maximum_unreliable_payload_size =
        config.maximum_datagram_size - goldsrc::kNetchanHeaderSize + 1U;
    invalid(config);
    config = test_config();
    config.maximum_datagrams_per_update = 0U;
    invalid(config);
    config = test_config();
    config.maximum_datagrams_per_update =
        goldsrc::kMaximumNetchanDriverDatagramsPerUpdate + 1U;
    invalid(config);
    config = test_config();
    config.maximum_outgoing_packets_per_update = 0U;
    invalid(config);
    config = test_config();
    config.maximum_outgoing_packets_per_update =
        goldsrc::kMaximumNetchanDriverOutgoingPacketsPerUpdate + 1U;
    invalid(config);
    config = test_config();
    config.maximum_events = goldsrc::kMinimumNetchanDriverEvents - 1U;
    invalid(config);
    config = test_config();
    config.maximum_events = goldsrc::kMaximumNetchanDriverEvents + 1U;
    invalid(config);
}

TEST_CASE("Netchan driver starts without TX and would-block update is bounded",
          "[goldsrc][netchan][driver]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    std::size_t releases = 0U;
    goldsrc::NetchanDriver driver{
        transport,
        remote,
        test_config(),
        std::make_unique<CountingLifetime>(releases)};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;

    require_started(driver, transport, epoch);
    CHECK(releases == 0U);
    driver.update(epoch + 1ms);
    CHECK(driver.state() == goldsrc::NetchanDriverState::active);
    CHECK(transport.receive_calls == 1U);
    REQUIRE(transport.receive_limits.size() == 1U);
    CHECK(transport.receive_limits.front() == test_config().maximum_datagram_size);
    CHECK(transport.sent.empty());
    CHECK(driver.pending_event_count() == 0U);
    CHECK(releases == 0U);
}

TEST_CASE("Netchan driver permits bounded client-first reliable data only",
          "[goldsrc][netchan][driver][reliable][client-first]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    const auto reliable = bytes("FIRST");
    const auto held_unreliable = bytes("HELD_UNRELIABLE");
    REQUIRE(driver.queue_reliable(reliable));
    REQUIRE(driver.submit_unreliable(held_unreliable));

    driver.update(epoch + 1ms);

    REQUIRE(transport.sent.size() == 1U);
    auto initial = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(initial);
    REQUIRE(initial.packet);
    CHECK(initial.packet->header.sequence.sequence == sequence(1U));
    CHECK(initial.packet->header.sequence.flags.reliable);
    CHECK_FALSE(initial.packet->header.sequence.flags.fragmented);
    CHECK(initial.packet->header.acknowledgement.sequence == sequence(0U));
    CHECK_FALSE(initial.packet->header.acknowledgement.reliable);
    REQUIRE(initial.packet->payload.size() ==
            goldsrc::kStockProtocol48MinimumDecodedPayloadSize);
    CHECK(std::ranges::equal(
        reliable,
        std::span<const std::byte>{initial.packet->payload}.first(reliable.size())));
    CHECK(std::ranges::all_of(
        std::span<const std::byte>{initial.packet->payload}.subspan(reliable.size()),
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
    REQUIRE(driver.session().in_flight_reliable_payload());
    CHECK(driver.session().in_flight_reliable_payload()->bytes == reliable);
    CHECK_FALSE(driver.session().first_incoming_committed());
    CHECK_FALSE(driver.session().first_acknowledgement_sent());

    transport.queue(
        remote,
        server_packet(1U, true, 1U, true, bytes("SERVER_FIRST")));
    driver.update(epoch + 2ms);

    REQUIRE(transport.sent.size() == 2U);
    auto first_ack = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.back().payload);
    REQUIRE(first_ack);
    REQUIRE(first_ack.packet);
    CHECK(first_ack.packet->header.sequence.sequence == sequence(2U));
    CHECK(first_ack.packet->header.acknowledgement.sequence == sequence(1U));
    CHECK(first_ack.packet->header.acknowledgement.reliable);
    CHECK_FALSE(first_ack.packet->header.sequence.flags.reliable);
    CHECK_FALSE(driver.session().in_flight_reliable_payload());
    auto acknowledged = driver.poll_event();
    REQUIRE(acknowledged);
    CHECK(acknowledged->type ==
          goldsrc::NetchanDriverEventType::reliable_payload_acknowledged);
    auto payload = driver.poll_event();
    REQUIRE(payload);
    CHECK(payload->type == goldsrc::NetchanDriverEventType::payload_ready);
    REQUIRE(payload->payload);
    CHECK(payload->payload->bytes == bytes("SERVER_FIRST"));
    CHECK_FALSE(driver.poll_event());

    // The unrelated one-shot payload was held until the ordinary channel
    // acknowledgement existed and is emitted only on a later bounded update.
    driver.update(epoch + 3ms);
    REQUIRE(transport.sent.size() == 3U);
    auto later = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.back().payload);
    REQUIRE(later);
    REQUIRE(later.packet);
    CHECK(later.packet->header.sequence.sequence == sequence(3U));
    CHECK(std::ranges::equal(
        held_unreliable,
        std::span<const std::byte>{later.packet->payload}.first(
            held_unreliable.size())));
}

TEST_CASE("Netchan driver preserves a wildcard-bound local endpoint",
          "[goldsrc][netchan][driver][endpoint][same-socket]")
{
    FakeTransport transport;
    transport.local = network::NetworkAddress{0U, 30'000U};
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;

    require_started(driver, transport, epoch);
    CHECK(driver.local_endpoint() == transport.local);
    driver.update(epoch + 1ms);
    CHECK(driver.state() == goldsrc::NetchanDriverState::active);
    CHECK(transport.sent.empty());
}

TEST_CASE("Trace callback reentry cannot mutate driver state and thrown callbacks reset",
          "[goldsrc][netchan][driver][trace][reentry]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver* driver_pointer = nullptr;
    std::optional<goldsrc::NetchanDriverOperationResult> queued_result;
    std::optional<goldsrc::NetchanDriverOperationResult> submitted_result;
    std::size_t callback_attempts = 0U;
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{
        transport,
        remote,
        test_config(),
        {},
        [&](const goldsrc::NetchanDriverTraceEvent& event) {
            if (event.classification !=
                    goldsrc::NetchanDriverTraceClassification::
                        sequenced_packet_received ||
                driver_pointer == nullptr) {
                return;
            }
            ++callback_attempts;
            queued_result = driver_pointer->queue_reliable(bytes("REENTRANT_RELIABLE"));
            submitted_result =
                driver_pointer->submit_unreliable(bytes("REENTRANT_UNRELIABLE"));
            driver_pointer->cancel(epoch + 20ms);
            driver_pointer->update(epoch + 20ms);
            throw std::runtime_error{"synthetic trace callback exception"};
        }};
    driver_pointer = &driver;
    require_started(driver, transport, epoch);

    transport.queue(
        remote,
        server_packet(1U, true, 0U, false, bytes("ONLY_WIRE_PAYLOAD")));
    driver.update(epoch + 1ms);

    CHECK(callback_attempts == 1U);
    REQUIRE(queued_result);
    REQUIRE_FALSE(*queued_result);
    REQUIRE(queued_result->error);
    CHECK(queued_result->error->code ==
          goldsrc::NetchanDriverErrorCode::reentrant_operation);
    REQUIRE(submitted_result);
    REQUIRE_FALSE(*submitted_result);
    REQUIRE(submitted_result->error);
    CHECK(submitted_result->error->code ==
          goldsrc::NetchanDriverErrorCode::reentrant_operation);
    CHECK(driver.state() == goldsrc::NetchanDriverState::active);
    CHECK(driver.session().pending_reliable_payload().empty());
    CHECK_FALSE(driver.session().in_flight_reliable_payload());
    CHECK(driver.session().state().incoming_sequence == sequence(1U));
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent.front().payload == exact_first_acknowledgement_bytes());

    REQUIRE(driver.queue_reliable(bytes("AFTER_CALLBACK")));
    CHECK_FALSE(driver.session().pending_reliable_payload().empty());
}

TEST_CASE("Invalid driver start releases its opaque lifetime guard exactly once",
          "[goldsrc][netchan][driver][configuration][auth]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    auto config = test_config();
    config.maximum_events = 0U;
    std::size_t releases = 0U;
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    {
        goldsrc::NetchanDriver driver{
            transport,
            remote,
            config,
            std::make_unique<CountingLifetime>(releases)};
        REQUIRE_FALSE(driver.valid_configuration());
        REQUIRE(transport.local);
        CHECK_FALSE(driver.start(epoch, *transport.local));
        CHECK(driver.state() == goldsrc::NetchanDriverState::protocol_error);
        CHECK(driver.cleanup_count() == 1U);
        CHECK(releases == 1U);
        driver.update(epoch + 1ms);
        driver.cancel(epoch + 2ms);
        CHECK(releases == 1U);
    }
    CHECK(releases == 1U);
}

TEST_CASE("Netchan driver owns first payload and preserves the exact first ACK",
          "[goldsrc][netchan][driver][ack]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    const auto canonical = bytes("OPAQUE_FIRST_PAYLOAD");
    transport.queue(remote, server_packet(1U, true, 0U, false, canonical));
    driver.update(epoch + 1ms);

    REQUIRE(driver.state() == goldsrc::NetchanDriverState::active);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent.front().destination == remote);
    CHECK(transport.sent.front().payload == exact_first_acknowledgement_bytes());
    CHECK(driver.transmitted_packet_count() == 1U);
    CHECK(driver.session().first_acknowledgement_sent());
    CHECK(driver.session().state().incoming_sequence == sequence(1U));

    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::payload_ready);
    REQUIRE(event->payload);
    CHECK(event->payload->bytes == canonical);
    CHECK(event->payload->source_sequence == sequence(1U));
    CHECK(event->payload->source_acknowledgement == sequence(0U));
    CHECK(event->payload->sequence_flags.reliable);
    CHECK(event->payload->acknowledgement_reliable == false);
    CHECK(event->payload->direction == goldsrc::NetchanDirection::server_to_client);
    CHECK_FALSE(driver.poll_event());

    driver.update(epoch + 2ms);
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("Netchan driver enforces receive and outgoing update bounds",
          "[goldsrc][netchan][driver][bounds]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto rogue = network::NetworkAddress::loopback(27'016U);
    auto config = test_config();
    config.maximum_datagrams_per_update = 2U;
    config.maximum_outgoing_packets_per_update = 1U;
    goldsrc::NetchanDriver driver{transport, remote, config};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    transport.queue(rogue, server_packet(1U, true, 0U, false, bytes("ROGUE")));
    transport.queue(remote, server_packet(1U, true, 0U, false, bytes("ONE")));
    transport.queue(remote, server_packet(2U, true, 1U, false, bytes("TWO")));
    driver.update(epoch + 1ms);

    CHECK(transport.receive_calls == 2U);
    CHECK(transport.incoming.size() == 1U);
    CHECK(transport.sent.size() == 1U);
    CHECK(driver.pending_event_count() == 1U);

    driver.update(epoch + 2ms);
    CHECK(transport.incoming.empty());
    CHECK(transport.sent.size() == 2U);
    CHECK(driver.pending_event_count() == 2U);
}

TEST_CASE("Persistent driver consumes multiple target datagrams within all budgets",
          "[goldsrc][netchan][driver][bounds][persistent]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    auto config = test_config();
    config.maximum_datagrams_per_update = 2U;
    config.maximum_outgoing_packets_per_update = 8U;
    REQUIRE_FALSE(config.yield_after_owning_payload);
    goldsrc::NetchanDriver driver{transport, remote, config};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    transport.queue(remote, server_packet(1U, true, 0U, false, bytes("ONE")));
    transport.queue(remote, server_packet(2U, false, 1U, false, bytes("TWO")));
    transport.queue(remote, server_packet(3U, false, 1U, false, bytes("THREE")));

    driver.update(epoch + 1ms);
    CHECK(transport.receive_calls == 2U);
    CHECK(transport.incoming.size() == 1U);
    CHECK(transport.sent.size() == 1U);
    CHECK(driver.pending_event_count() == 2U);
    CHECK(driver.session().state().incoming_sequence == sequence(2U));

    driver.update(epoch + 2ms);
    CHECK(transport.incoming.empty());
    CHECK(transport.sent.size() == 1U);
    CHECK(driver.pending_event_count() == 3U);
    CHECK(driver.session().state().incoming_sequence == sequence(3U));
}

TEST_CASE("Persistent driver does not consume another target after TX budget is spent",
          "[goldsrc][netchan][driver][bounds][tx]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    auto config = test_config();
    config.maximum_datagrams_per_update = 8U;
    config.maximum_outgoing_packets_per_update = 1U;
    goldsrc::NetchanDriver driver{transport, remote, config};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    transport.queue(remote, server_packet(1U, true, 0U, false, bytes("ONE")));
    transport.queue(remote, server_packet(2U, true, 1U, false, bytes("TWO")));
    driver.update(epoch + 1ms);

    CHECK(transport.receive_calls == 1U);
    CHECK(transport.incoming.size() == 1U);
    CHECK(transport.sent.size() == 1U);
    CHECK(driver.session().state().incoming_sequence == sequence(1U));
}

TEST_CASE("Wrong endpoint cannot refresh channel activity",
          "[goldsrc][netchan][driver][endpoint][timeout]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto rogue = network::NetworkAddress::loopback(27'016U);
    std::size_t releases = 0U;
    goldsrc::NetchanDriver driver{
        transport,
        remote,
        test_config(),
        std::make_unique<CountingLifetime>(releases)};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);
    const auto initial_activity = driver.last_valid_packet_time();

    transport.queue(rogue, server_packet(1U, true, 0U, false, bytes("ROGUE")));
    driver.update(epoch + 99ms);
    CHECK(driver.state() == goldsrc::NetchanDriverState::active);
    CHECK(driver.last_valid_packet_time() == initial_activity);
    CHECK(transport.sent.empty());
    CHECK(driver.pending_event_count() == 0U);

    driver.update(epoch + 100ms);
    CHECK(driver.state() == goldsrc::NetchanDriverState::timed_out);
    REQUIRE(driver.last_error());
    CHECK(driver.last_error()->code ==
          goldsrc::NetchanDriverErrorCode::channel_inactivity_timed_out);
    CHECK(driver.cleanup_count() == 1U);
    CHECK(releases == 1U);
    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::channel_timed_out);
}

TEST_CASE("Netchan driver terminal cleanup is immediate and idempotent",
          "[goldsrc][netchan][driver][cleanup][auth]")
{
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;

    SECTION("cancel clears reliable state and releases lifetime once")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::NetchanDriver driver{
            transport,
            remote,
            test_config(),
            std::make_unique<CountingLifetime>(releases)};
        require_started(driver, transport, epoch);
        REQUIRE(driver.queue_reliable(bytes("RELIABLE_PENDING")));
        REQUIRE_FALSE(driver.session().pending_reliable_payload().empty());

        driver.cancel(epoch + 1ms);
        CHECK(driver.state() == goldsrc::NetchanDriverState::cancelled);
        CHECK(driver.terminal());
        CHECK(driver.session().pending_reliable_payload().empty());
        CHECK_FALSE(driver.session().in_flight_reliable_payload());
        CHECK(driver.cleanup_count() == 1U);
        CHECK(releases == 1U);

        const auto receives = transport.receive_calls;
        driver.update(epoch + 2ms);
        driver.cancel(epoch + 3ms);
        driver.close(epoch + 4ms);
        CHECK(transport.receive_calls == receives);
        CHECK(driver.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("close releases a live lifetime and destructor does not repeat it")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        {
            goldsrc::NetchanDriver driver{
                transport,
                remote,
                test_config(),
                std::make_unique<CountingLifetime>(releases)};
            require_started(driver, transport, epoch);
            driver.close(epoch + 1ms);
            CHECK(driver.state() == goldsrc::NetchanDriverState::closed);
            CHECK(driver.cleanup_count() == 1U);
            CHECK(releases == 1U);
        }
        CHECK(releases == 1U);
    }

    SECTION("destruction is a terminal lifetime boundary")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        {
            goldsrc::NetchanDriver driver{
                transport,
                remote,
                test_config(),
                std::make_unique<CountingLifetime>(releases)};
            require_started(driver, transport, epoch);
            CHECK(releases == 0U);
        }
        CHECK(releases == 1U);
    }
}

TEST_CASE("Netchan driver maps receive and first-ACK send failures",
          "[goldsrc][netchan][driver][network]")
{
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;

    SECTION("receive error")
    {
        FakeTransport transport;
        goldsrc::NetchanDriver driver{transport, remote, test_config()};
        require_started(driver, transport, epoch);
        transport.queue_receive_error();
        driver.update(epoch + 1ms);
        CHECK(driver.state() == goldsrc::NetchanDriverState::network_error);
        REQUIRE(driver.last_error());
        CHECK(driver.last_error()->code == goldsrc::NetchanDriverErrorCode::receive_failed);
        CHECK(driver.cleanup_count() == 1U);
    }

    SECTION("send error does not deliver a payload event")
    {
        FakeTransport transport;
        goldsrc::NetchanDriver driver{transport, remote, test_config()};
        require_started(driver, transport, epoch);
        transport.send_results.push_back(network::DatagramSendResult{
            network::DatagramSendStatus::error,
            "synthetic send failure",
        });
        transport.queue(remote, server_packet(1U, true, 0U, false, bytes("PAYLOAD")));
        driver.update(epoch + 1ms);
        CHECK(driver.state() == goldsrc::NetchanDriverState::network_error);
        REQUIRE(driver.last_error());
        CHECK(driver.last_error()->code == goldsrc::NetchanDriverErrorCode::send_failed);
        CHECK(driver.transmitted_packet_count() == 0U);
        auto event = driver.poll_event();
        REQUIRE(event);
        CHECK(event->type == goldsrc::NetchanDriverEventType::network_error);
        CHECK_FALSE(driver.poll_event());
    }
}

TEST_CASE("Target-endpoint malformed packet is terminal without ACK or payload",
          "[goldsrc][netchan][driver][protocol][malformed]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::NetchanDriver driver{
        transport,
        remote,
        test_config(),
        std::make_unique<CountingLifetime>(releases)};
    require_started(driver, transport, epoch);
    transport.queue(
        remote,
        std::vector<std::byte>(goldsrc::kNetchanHeaderSize - 1U));

    driver.update(epoch + 1ms);

    CHECK(driver.state() == goldsrc::NetchanDriverState::protocol_error);
    REQUIRE(driver.last_error());
    CHECK(driver.last_error()->code ==
          goldsrc::NetchanDriverErrorCode::malformed_packet);
    CHECK(driver.session().state().incoming_sequence == sequence(0U));
    CHECK_FALSE(driver.normal_reassembler().active_transfer());
    CHECK(driver.normal_reassembler().ranges().empty());
    CHECK(transport.sent.empty());
    CHECK(driver.transmitted_packet_count() == 0U);
    CHECK(driver.cleanup_count() == 1U);
    CHECK(releases == 1U);
    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::protocol_error);
    CHECK_FALSE(event->payload);
    CHECK_FALSE(driver.poll_event());

    const auto receive_calls = transport.receive_calls;
    driver.update(epoch + 2ms);
    driver.cancel(epoch + 3ms);
    driver.close(epoch + 4ms);
    CHECK(transport.receive_calls == receive_calls);
    CHECK(transport.sent.empty());
    CHECK(driver.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Netchan driver reassembles out-of-order normal fragments exactly once",
          "[goldsrc][netchan][driver][fragment][reassembly][project-policy]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    const auto canonical = canonical_payload(2U *
                                                 goldsrc::kStockProtocol48NormalFragmentChunkSize +
                                             5U);
    const auto second = canonical_range(
        canonical,
        goldsrc::kStockProtocol48NormalFragmentChunkSize,
        goldsrc::kStockProtocol48NormalFragmentChunkSize);
    transport.queue(
        remote,
        server_fragment_packet(1U, 0U, false, 2U, 3U, second));
    driver.update(epoch + 1ms);

    REQUIRE(driver.state() == goldsrc::NetchanDriverState::active);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent.front().payload == exact_first_acknowledgement_bytes());
    CHECK(driver.session().state().incoming_reliable_acknowledgement);
    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::normal_transfer_started);
    CHECK(event->fragment_offset ==
          goldsrc::kStockProtocol48NormalFragmentChunkSize);
    CHECK(event->fragment_length ==
          goldsrc::kStockProtocol48NormalFragmentChunkSize);
    CHECK_FALSE(driver.poll_event());
    REQUIRE(driver.normal_reassembler().active_transfer());

    const auto activity_after_first = driver.last_valid_packet_time();
    transport.queue(
        remote,
        server_fragment_packet(1U, 0U, false, 2U, 3U, second));
    driver.update(epoch + 2ms);
    CHECK(transport.sent.size() == 1U);
    CHECK(driver.pending_event_count() == 0U);
    CHECK(driver.last_valid_packet_time() == activity_after_first);
    CHECK(driver.normal_reassembler().ranges().size() == 1U);

    transport.queue(
        remote,
        server_fragment_packet(
            2U,
            1U,
            false,
            1U,
            3U,
            canonical_range(
                canonical,
                0U,
                goldsrc::kStockProtocol48NormalFragmentChunkSize)));
    driver.update(epoch + 3ms);
    REQUIRE(transport.sent.size() == 2U);
    auto second_ack = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.back().payload);
    REQUIRE(second_ack);
    REQUIRE(second_ack.packet);
    CHECK(second_ack.packet->header.acknowledgement.sequence == sequence(2U));
    CHECK_FALSE(second_ack.packet->header.acknowledgement.reliable);
    CHECK(driver.pending_event_count() == 0U);

    transport.queue(
        remote,
        server_fragment_packet(
            3U,
            2U,
            false,
            3U,
            3U,
            canonical_range(
                canonical,
                2U * goldsrc::kStockProtocol48NormalFragmentChunkSize,
                5U)));
    driver.update(epoch + 4ms);
    REQUIRE(transport.sent.size() == 3U);
    auto final_ack = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.back().payload);
    REQUIRE(final_ack);
    REQUIRE(final_ack.packet);
    CHECK(final_ack.packet->header.acknowledgement.sequence == sequence(3U));
    CHECK(final_ack.packet->header.acknowledgement.reliable);

    event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::normal_transfer_completed);
    CHECK(event->transfer_size == canonical.size());
    event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::payload_ready);
    REQUIRE(event->payload);
    CHECK(event->payload->bytes == canonical);
    CHECK(event->payload->source_sequence == sequence(3U));
    CHECK_FALSE(driver.poll_event());
    CHECK_FALSE(driver.normal_reassembler().active_transfer());
}

TEST_CASE("Fresh-sequence exact fragment retry advances numeric ACK without retoggle",
          "[goldsrc][netchan][driver][fragment][retransmission]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    const auto fragment = canonical_payload(
        goldsrc::kStockProtocol48NormalFragmentChunkSize);
    transport.queue(
        remote,
        server_fragment_packet(1U, 0U, false, 1U, 2U, fragment));
    driver.update(epoch + 1ms);
    REQUIRE(driver.poll_event());
    REQUIRE(driver.normal_reassembler().active_transfer());
    const auto original_last_fragment_at =
        driver.normal_reassembler().active_transfer()->last_fragment_at;
    CHECK(driver.session().state().incoming_reliable_acknowledgement);

    transport.queue(
        remote,
        server_fragment_packet(2U, 1U, false, 1U, 2U, fragment));
    driver.update(epoch + 20ms);
    CHECK(driver.session().state().incoming_sequence == sequence(2U));
    CHECK(driver.session().state().incoming_reliable_acknowledgement);
    REQUIRE(driver.normal_reassembler().active_transfer());
    CHECK(driver.normal_reassembler().active_transfer()->last_fragment_at ==
          original_last_fragment_at);
    CHECK(driver.normal_reassembler().ranges().size() == 1U);
    CHECK_FALSE(driver.poll_event());
    REQUIRE(transport.sent.size() == 2U);
    auto retry_ack = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.back().payload);
    REQUIRE(retry_ack);
    REQUIRE(retry_ack.packet);
    CHECK(retry_ack.packet->header.acknowledgement.sequence == sequence(2U));
    CHECK(retry_ack.packet->header.acknowledgement.reliable);
}

TEST_CASE("Fragment suffix is preserved separately from owning completion",
          "[goldsrc][netchan][driver][fragment][suffix]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    const auto fragment = bytes("FRAGMENTED_NETCHAN_TEST_PAYLOAD");
    const auto suffix = bytes("ORDINARY_SUFFIX");
    transport.queue(
        remote,
        server_fragment_packet(1U, 0U, false, 1U, 1U, fragment, suffix));
    driver.update(epoch + 1ms);

    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent.front().payload == exact_first_acknowledgement_bytes());
    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::normal_transfer_started);
    event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::normal_transfer_completed);
    event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::payload_ready);
    REQUIRE(event->payload);
    CHECK(event->payload->bytes == fragment);
    event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::payload_ready);
    REQUIRE(event->payload);
    CHECK(event->payload->bytes == suffix);
    CHECK_FALSE(driver.poll_event());
}

TEST_CASE("Fragment suffix obeys the owning payload bound before state commit",
          "[goldsrc][netchan][driver][fragment][suffix][bounds]")
{
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_opaque_payload_size = 16U;

    SECTION("exact bound")
    {
        FakeTransport transport;
        goldsrc::NetchanDriver driver{transport, remote, config};
        require_started(driver, transport, epoch);
        transport.queue(
            remote,
            server_fragment_packet(
                1U,
                0U,
                false,
                1U,
                1U,
                bytes("FRAGMENT"),
                std::vector<std::byte>(16U, std::byte{0x53})));
        driver.update(epoch + 1ms);
        CHECK(driver.state() == goldsrc::NetchanDriverState::active);
        CHECK(driver.session().state().incoming_sequence == sequence(1U));
        CHECK_FALSE(driver.normal_reassembler().active_transfer());
        CHECK(transport.sent.size() == 1U);
    }

    SECTION("bound plus one")
    {
        FakeTransport transport;
        goldsrc::NetchanDriver driver{transport, remote, config};
        require_started(driver, transport, epoch);
        transport.queue(
            remote,
            server_fragment_packet(
                1U,
                0U,
                false,
                1U,
                1U,
                bytes("FRAGMENT"),
                std::vector<std::byte>(17U, std::byte{0x53})));
        driver.update(epoch + 1ms);
        CHECK(driver.state() == goldsrc::NetchanDriverState::protocol_error);
        REQUIRE(driver.last_error());
        CHECK(driver.last_error()->code ==
              goldsrc::NetchanDriverErrorCode::opaque_payload_too_large);
        CHECK(driver.session().state().incoming_sequence == sequence(0U));
        CHECK_FALSE(driver.normal_reassembler().active_transfer());
        CHECK(driver.normal_reassembler().ranges().empty());
        CHECK(transport.sent.empty());
    }
}

TEST_CASE("Secondary fragment slot is terminal pending-M3 without byte retention",
          "[goldsrc][netchan][driver][fragment][secondary]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    transport.queue(
        remote,
        server_fragment_packet(
            1U,
            0U,
            false,
            1U,
            1U,
            bytes("SECONDARY_BYTES_MUST_NOT_ESCAPE"),
            {},
            1U));
    driver.update(epoch + 1ms);

    CHECK(driver.state() == goldsrc::NetchanDriverState::protocol_error);
    REQUIRE(driver.last_error());
    CHECK(driver.last_error()->code ==
          goldsrc::NetchanDriverErrorCode::secondary_stream_pending_m3);
    REQUIRE(driver.last_error()->reassembly_code);
    CHECK(*driver.last_error()->reassembly_code ==
          goldsrc::NetchanReassemblyErrorCode::secondary_stream_pending_m3);
    CHECK(driver.session().state().incoming_sequence == sequence(0U));
    CHECK_FALSE(driver.normal_reassembler().active_transfer());
    CHECK(transport.sent.empty());
    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type ==
          goldsrc::NetchanDriverEventType::secondary_stream_pending_m3);
    CHECK_FALSE(event->payload);
    CHECK_FALSE(driver.poll_event());
}

TEST_CASE("Fragment ACK failure clears partial state before publishing events",
          "[goldsrc][netchan][driver][fragment][network][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);
    transport.send_results.push_back(network::DatagramSendResult{
        network::DatagramSendStatus::error,
        "synthetic fragment ACK failure",
    });
    transport.queue(
        remote,
        server_fragment_packet(
            1U,
            0U,
            false,
            1U,
            1U,
            bytes("MUST_NOT_BE_PUBLISHED")));
    driver.update(epoch + 1ms);

    CHECK(driver.state() == goldsrc::NetchanDriverState::network_error);
    CHECK_FALSE(driver.normal_reassembler().active_transfer());
    CHECK(driver.normal_reassembler().ranges().empty());
    CHECK(driver.session().pending_reliable_payload().empty());
    CHECK_FALSE(driver.session().in_flight_reliable_payload());
    CHECK(transport.sent.empty());
    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::network_error);
    CHECK_FALSE(event->payload);
    CHECK_FALSE(driver.poll_event());
}

TEST_CASE("Normal fragment deadline is fixed and expires without partial delivery",
          "[goldsrc][netchan][driver][fragment][timeout]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    transport.queue(
        remote,
        server_fragment_packet(
            1U,
            0U,
            false,
            1U,
            3U,
            canonical_payload(
                goldsrc::kStockProtocol48NormalFragmentChunkSize)));
    driver.update(epoch + 1ms);
    auto event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type == goldsrc::NetchanDriverEventType::normal_transfer_started);
    REQUIRE(driver.normal_reassembler().active_transfer());
    const auto fixed_deadline =
        driver.normal_reassembler().active_transfer()->deadline;

    transport.queue(
        remote,
        server_fragment_packet(
            2U,
            1U,
            false,
            2U,
            3U,
            canonical_payload(
                goldsrc::kStockProtocol48NormalFragmentChunkSize)));
    driver.update(epoch + 40ms);
    REQUIRE(driver.normal_reassembler().active_transfer());
    CHECK(driver.normal_reassembler().active_transfer()->deadline == fixed_deadline);
    CHECK(driver.normal_reassembler().active_transfer()->last_fragment_at ==
          epoch + 40ms);
    CHECK_FALSE(driver.poll_event());

    driver.update(fixed_deadline);
    CHECK(driver.state() == goldsrc::NetchanDriverState::timed_out);
    REQUIRE(driver.last_error());
    CHECK(driver.last_error()->code ==
          goldsrc::NetchanDriverErrorCode::fragment_transfer_timed_out);
    CHECK_FALSE(driver.normal_reassembler().active_transfer());
    CHECK(driver.normal_reassembler().ranges().empty());
    event = driver.poll_event();
    REQUIRE(event);
    CHECK(event->type ==
          goldsrc::NetchanDriverEventType::normal_transfer_timed_out);
    CHECK_FALSE(event->payload);
    CHECK_FALSE(driver.poll_event());
}

TEST_CASE("Netchan event backpressure occurs before a queued packet mutates session state",
          "[goldsrc][netchan][driver][backpressure]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    auto config = test_config();
    config.maximum_events = goldsrc::kMinimumNetchanDriverEvents;
    std::size_t releases = 0U;
    goldsrc::NetchanDriver driver{
        transport,
        remote,
        config,
        std::make_unique<CountingLifetime>(releases)};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    for (std::uint32_t packet_sequence = 1U;
         packet_sequence <= config.maximum_events;
         ++packet_sequence) {
        transport.queue(
            remote,
            server_packet(
                packet_sequence,
                packet_sequence == 1U,
                packet_sequence == 1U ? 0U : 1U,
                false,
                bytes("QUEUED")));
        driver.update(epoch + std::chrono::milliseconds{packet_sequence});
        REQUIRE(driver.state() == goldsrc::NetchanDriverState::active);
    }
    REQUIRE(driver.pending_event_count() == config.maximum_events);
    REQUIRE(driver.session().state().incoming_sequence ==
            sequence(static_cast<std::uint32_t>(config.maximum_events)));

    const auto next_sequence =
        static_cast<std::uint32_t>(config.maximum_events + 1U);
    transport.queue(
        remote,
        server_packet(
            next_sequence,
            false,
            static_cast<std::uint32_t>(config.maximum_events),
            false,
            bytes("SECOND")));
    const auto receives_before = transport.receive_calls;
    driver.update(epoch + 20ms);
    CHECK(driver.state() == goldsrc::NetchanDriverState::backpressure);
    REQUIRE(driver.last_error());
    CHECK(driver.last_error()->code == goldsrc::NetchanDriverErrorCode::event_backpressure);
    CHECK(driver.session().state().incoming_sequence ==
          sequence(static_cast<std::uint32_t>(config.maximum_events)));
    CHECK(transport.receive_calls == receives_before);
    CHECK(transport.incoming.size() == 1U);
    CHECK(driver.cleanup_count() == 1U);
    CHECK(releases == 1U);

    for (std::size_t index = 0U; index < config.maximum_events; ++index) {
        auto event = driver.poll_event();
        REQUIRE(event);
        CHECK(event->type == goldsrc::NetchanDriverEventType::payload_ready);
    }
    CHECK_FALSE(driver.poll_event());
}

TEST_CASE("Fragment event preflight rejects before session or reassembly commit",
          "[goldsrc][netchan][driver][fragment][backpressure][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    auto config = test_config();
    config.maximum_events = goldsrc::kMinimumNetchanDriverEvents;
    std::size_t releases = 0U;
    goldsrc::NetchanDriver driver{
        transport,
        remote,
        config,
        std::make_unique<CountingLifetime>(releases)};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    for (std::uint32_t packet_sequence = 1U;
         packet_sequence <= 3U;
         ++packet_sequence) {
        transport.queue(
            remote,
            server_packet(
                packet_sequence,
                packet_sequence == 1U,
                packet_sequence == 1U ? 0U : 1U,
                false,
                bytes("FILL_EVENT_QUEUE")));
        driver.update(epoch + std::chrono::milliseconds{packet_sequence});
    }
    REQUIRE(driver.pending_event_count() == 3U);
    REQUIRE(driver.session().state().incoming_sequence == sequence(3U));
    REQUIRE_FALSE(driver.normal_reassembler().active_transfer());
    const auto sends_before = transport.sent.size();

    transport.queue(
        remote,
        server_fragment_packet(
            4U,
            1U,
            false,
            1U,
            1U,
            bytes("FRAGMENT_COMPLETION"),
            bytes("ORDINARY_SUFFIX")));
    driver.update(epoch + 10ms);

    CHECK(driver.state() == goldsrc::NetchanDriverState::backpressure);
    REQUIRE(driver.last_error());
    CHECK(driver.last_error()->code ==
          goldsrc::NetchanDriverErrorCode::event_backpressure);
    CHECK(driver.session().state().incoming_sequence == sequence(3U));
    CHECK_FALSE(driver.normal_reassembler().active_transfer());
    CHECK(driver.normal_reassembler().ranges().empty());
    CHECK(transport.sent.size() == sends_before);
    CHECK(driver.pending_event_count() == 3U);
    CHECK(driver.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Netchan driver retains one bounded unreliable payload until post-bootstrap TX",
          "[goldsrc][netchan][driver][outgoing]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    goldsrc::NetchanDriver driver{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    const auto unreliable = bytes("ONE_SHOT_UNRELIABLE");
    REQUIRE(driver.submit_unreliable(unreliable));
    const auto duplicate_submit = driver.submit_unreliable(bytes("SECOND"));
    REQUIRE_FALSE(duplicate_submit);
    REQUIRE(duplicate_submit.error);
    CHECK(duplicate_submit.error->code ==
          goldsrc::NetchanDriverErrorCode::unreliable_payload_pending);

    driver.update(epoch + 1ms);
    CHECK(transport.sent.empty());

    transport.queue(remote, server_packet(1U, true, 0U, false, bytes("BOOTSTRAP")));
    driver.update(epoch + 2ms);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent.front().payload == exact_first_acknowledgement_bytes());

    driver.update(epoch + 3ms);
    REQUIRE(transport.sent.size() == 2U);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.back().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.sequence == sequence(2U));
    CHECK_FALSE(decoded.packet->header.sequence.flags.reliable);
    CHECK(decoded.packet->payload.size() >= unreliable.size());
    CHECK(std::ranges::equal(
        unreliable,
        std::span<const std::byte>{decoded.packet->payload}.first(unreliable.size())));

    REQUIRE(driver.submit_unreliable(bytes("AFTER_SEND")));
}

TEST_CASE("Outgoing normal fragments are stop-and-wait and retry only the missing unit",
          "[goldsrc][netchan][driver][fragment][outgoing][project-policy]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    auto config = test_config();
    config.maximum_datagram_size = 1'100U;
    config.maximum_fragment_datagram_size = 1'100U;
    config.maximum_unreliable_payload_size =
        config.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    std::vector<goldsrc::NetchanDriverTraceEvent> trace;
    goldsrc::NetchanDriver driver{
        transport,
        remote,
        config,
        {},
        [&](const goldsrc::NetchanDriverTraceEvent& event) {
            trace.push_back(event);
        }};
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    require_started(driver, transport, epoch);

    transport.queue(
        remote,
        server_packet(1U, true, 0U, false, bytes("BOOTSTRAP")));
    driver.update(epoch + 1ms);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent.front().payload == exact_first_acknowledgement_bytes());
    REQUIRE(driver.poll_event());
    CHECK_FALSE(driver.poll_event());

    const auto canonical = canonical_payload(
        2U * goldsrc::kStockProtocol48NormalFragmentChunkSize + 37U);
    REQUIRE(driver.queue_reliable(canonical));
    driver.update(epoch + 2ms);
    REQUIRE(transport.sent.size() == 2U);
    const auto first_fragment_trace = std::ranges::find_if(
        trace.rbegin(),
        trace.rend(),
        [](const goldsrc::NetchanDriverTraceEvent& event) {
            return event.classification ==
                       goldsrc::NetchanDriverTraceClassification::packet_sent &&
                   event.fragmented;
        });
    REQUIRE(first_fragment_trace != trace.rend());
    CHECK(first_fragment_trace->fragment_stream ==
          goldsrc::NetchanFragmentStream::normal);
    CHECK(first_fragment_trace->local_transfer_id.has_value());
    CHECK(first_fragment_trace->fragment_offset == 0U);
    CHECK(first_fragment_trace->fragment_length ==
          goldsrc::kStockProtocol48NormalFragmentChunkSize);
    CHECK(first_fragment_trace->covered_size ==
          goldsrc::kStockProtocol48NormalFragmentChunkSize);
    CHECK(first_fragment_trace->transfer_size == canonical.size());

    const auto decode_fragment = [&](const std::size_t sent_index) {
        auto decoded = goldsrc::decode_client_to_server_netchan_packet(
            transport.sent.at(sent_index).payload);
        REQUIRE(decoded);
        REQUIRE(decoded.packet);
        REQUIRE(decoded.packet->header.sequence.flags.fragmented);
        REQUIRE(decoded.packet->header.sequence.flags.reliable);
        REQUIRE(decoded.packet->fragments[0U]);
        CHECK_FALSE(decoded.packet->fragments[1U]);
        return std::move(*decoded.packet);
    };

    auto first = decode_fragment(1U);
    REQUIRE(first.fragments[0U]->packed_id());
    CHECK(first.fragments[0U]->packed_id()->fragment_index() == 1U);
    CHECK(first.fragments[0U]->packed_id()->fragment_count() == 3U);
    CHECK(first.fragment_payload_size ==
          goldsrc::kStockProtocol48NormalFragmentChunkSize);
    CHECK(std::ranges::equal(
        std::span<const std::byte>{first.payload}.first(first.fragment_payload_size),
        std::span<const std::byte>{canonical}.first(first.fragment_payload_size)));
    REQUIRE(driver.session().in_flight_reliable_payload());
    const bool first_toggle =
        driver.session().in_flight_reliable_payload()->toggle;

    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            first.header.sequence.sequence.value(),
            first_toggle,
            bytes("ACK_ONE_")));
    driver.update(epoch + 3ms);
    REQUIRE(transport.sent.size() == 3U);
    auto second = decode_fragment(2U);
    REQUIRE(second.fragments[0U]->packed_id());
    CHECK(second.fragments[0U]->packed_id()->fragment_index() == 2U);
    CHECK(second.fragments[0U]->packed_id()->fragment_count() == 3U);
    CHECK(second.payload == canonical_range(
                                canonical,
                                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                                goldsrc::kStockProtocol48NormalFragmentChunkSize));
    REQUIRE(driver.session().in_flight_reliable_payload());
    const bool second_toggle =
        driver.session().in_flight_reliable_payload()->toggle;

    REQUIRE(driver.submit_unreliable(bytes("ACK_GAP_PROBE")));
    driver.update(epoch + 4ms);
    REQUIRE(transport.sent.size() == 4U);
    auto gap_probe = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.back().payload);
    REQUIRE(gap_probe);
    REQUIRE(gap_probe.packet);
    CHECK_FALSE(gap_probe.packet->header.sequence.flags.fragmented);
    CHECK_FALSE(gap_probe.packet->header.sequence.flags.reliable);

    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            gap_probe.packet->header.sequence.sequence.value(),
            !second_toggle,
            bytes("ACK_GAP_")));
    driver.update(epoch + 5ms);
    REQUIRE(transport.sent.size() == 5U);
    auto second_retry = decode_fragment(4U);
    REQUIRE(second_retry.fragments[0U]->packed_id());
    CHECK(second_retry.fragments[0U]->packed_id() ==
          second.fragments[0U]->packed_id());
    CHECK(second_retry.payload == second.payload);
    CHECK(second_retry.header.sequence.sequence !=
          second.header.sequence.sequence);
    REQUIRE(driver.session().in_flight_reliable_payload());
    CHECK(driver.session().in_flight_reliable_payload()->toggle == second_toggle);

    transport.queue(
        remote,
        server_packet(
            4U,
            false,
            second_retry.header.sequence.sequence.value(),
            second_toggle,
            bytes("ACK_TWO_")));
    driver.update(epoch + 6ms);
    REQUIRE(transport.sent.size() == 6U);
    auto final_fragment = decode_fragment(5U);
    REQUIRE(final_fragment.fragments[0U]->packed_id());
    CHECK(final_fragment.fragments[0U]->packed_id()->fragment_index() == 3U);
    CHECK(final_fragment.fragments[0U]->packed_id()->fragment_count() == 3U);
    CHECK(final_fragment.payload == canonical_range(
                                        canonical,
                                        2U * goldsrc::kStockProtocol48NormalFragmentChunkSize,
                                        37U));
    REQUIRE(driver.session().in_flight_reliable_payload());
    const bool final_toggle =
        driver.session().in_flight_reliable_payload()->toggle;

    transport.queue(
        remote,
        server_packet(
            5U,
            false,
            final_fragment.header.sequence.sequence.value(),
            final_toggle,
            bytes("ACK_END_")));
    driver.update(epoch + 7ms);
    CHECK(transport.sent.size() == 6U);
    CHECK_FALSE(driver.session().outgoing_fragment_transfer());
    CHECK_FALSE(driver.session().in_flight_reliable_payload());
    CHECK(driver.session().pending_reliable_payload().empty());

    std::size_t acknowledged_events = 0U;
    while (auto event = driver.poll_event()) {
        if (event->type ==
            goldsrc::NetchanDriverEventType::reliable_payload_acknowledged) {
            ++acknowledged_events;
        }
    }
    CHECK(acknowledged_events == 1U);
}
