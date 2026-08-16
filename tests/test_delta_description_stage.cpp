#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/delta_description_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/network/datagram_transport.hpp>

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
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

struct SentDatagram {
    network::NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeTransport final : public network::IDatagramTransport {
public:
    [[nodiscard]] network::DatagramLocalAddressResult local_address() const override
    {
        return network::DatagramLocalAddressResult{local, {}};
    }

    [[nodiscard]] network::DatagramSendResult send_to(
        const network::NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        sent.push_back(SentDatagram{
            destination,
            std::vector<std::byte>{payload.begin(), payload.end()},
        });
        return {network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(std::size_t) override
    {
        ++receive_calls;
        if (incoming.empty()) {
            return {
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

    void queue(
        const network::NetworkAddress source,
        std::vector<std::byte> payload)
    {
        const auto size = payload.size();
        incoming.push_back({
            network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(payload)},
            source,
            size,
            {},
        });
    }

    void queue_error()
    {
        incoming.push_back({
            network::DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic delta-stage receive failure",
        });
    }

    network::NetworkAddress local{network::NetworkAddress::loopback(30'600U)};
    std::size_t receive_calls{0U};
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
};

class CountingLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept : releases_{releases} {}
    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
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
        std::byte{0x00U},
    };
    std::ranges::transform(
        compressed,
        std::back_inserter(envelope),
        [](const char value) {
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t acknowledgement,
    std::vector<std::byte> payload,
    const std::uint32_t server_sequence = 1U)
{
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(server_sequence),
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

[[nodiscard]] goldsrc::DeltaDescriptionStageConfig test_config()
{
    goldsrc::DeltaDescriptionStageConfig config;
    config.pre_resource.initial_signon.driver.channel_inactivity_timeout = 50ms;
    config.pre_resource.initial_signon.driver.fragment_transfer_timeout = 50ms;
    config.pre_resource.initial_signon.driver.maximum_datagrams_per_update = 8U;
    config.pre_resource.initial_signon.driver.maximum_outgoing_packets_per_update = 8U;
    config.pre_resource.initial_signon.driver.maximum_events = 32U;
    config.pre_resource.initial_signon.maximum_events = 32U;
    config.pre_resource.initial_signon.maximum_driver_events_per_update = 32U;
    config.pre_resource.maximum_events = 32U;
    config.maximum_events = 32U;
    return config;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket start_and_send_request(
    goldsrc::DeltaDescriptionStage& stage,
    FakeTransport& transport,
    const goldsrc::DeltaDescriptionStageTimePoint epoch,
    std::unique_ptr<goldsrc::INetchanDriverLifetime> lifetime = {})
{
    REQUIRE(stage.start(epoch, transport.local, std::move(lifetime)));
    CHECK(stage.state() ==
          goldsrc::DeltaDescriptionStageState::waiting_for_pre_resource_state);
    stage.update(epoch + 1ms);
    REQUIRE(transport.sent.size() == 1U);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    REQUIRE(decoded.packet->payload.size() == 8U);
    CHECK(decoded.packet->payload[0U] == std::byte{3U});
    CHECK(decoded.packet->payload[1U] == std::byte{'n'});
    CHECK(decoded.packet->payload[2U] == std::byte{'e'});
    CHECK(decoded.packet->payload[3U] == std::byte{'w'});
    CHECK(decoded.packet->payload[4U] == std::byte{0U});
    return std::move(*decoded.packet);
}

[[nodiscard]] std::vector<std::vector<std::byte>> two_schemas()
{
    return {
        fixture::schema("alpha_t", fixture::kSchemaAlphaFields),
        fixture::schema("bravo_t", fixture::kSchemaBravoFields),
    };
}

void deliver(
    goldsrc::DeltaDescriptionStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::ClientToServerNetchanPacket& request,
    std::vector<std::byte> semantic,
    const goldsrc::DeltaDescriptionStageTimePoint now)
{
    transport.queue(
        remote,
        server_packet(
            request.header.sequence.sequence.value(),
            service_envelope(semantic)));
    stage.update(now);
}

TEST_CASE("Delta description stage publishes registry and post-delta boundary once",
          "[goldsrc][delta][stage][success][lifetime]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'240U);
    const auto epoch = goldsrc::DeltaDescriptionStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    std::vector<goldsrc::DeltaDescriptionTraceEvent> trace;
    goldsrc::DeltaDescriptionStage stage{
        transport,
        remote,
        test_config(),
        {},
        {},
        [&](const goldsrc::DeltaDescriptionTraceEvent& event) {
            trace.push_back(event);
        }};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    CHECK(releases == 0U);

    const auto schemas = two_schemas();
    const auto semantic = fixture::service_payload(schemas);
    deliver(stage, transport, remote, request, semantic, epoch + 2ms);

    CHECK(stage.state() ==
          goldsrc::DeltaDescriptionStageState::post_delta_boundary_reached);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->pre_resource().server_info().protocol_version() ==
          goldsrc::ProtocolVersion::goldsrc_48);
    CHECK(stage.result()->registry().schema_count() == 2U);
    CHECK(stage.result()->registry().total_field_count() == 4U);
    CHECK(stage.result()->registry().schemas()[0U].name() == "alpha_t");
    CHECK(stage.result()->registry().schemas()[1U].name() == "bravo_t");
    CHECK(stage.result()->boundary().opcode() ==
          goldsrc::kStockPostDeltaBoundaryOpcode);
    CHECK(stage.result()->boundary().remaining_byte_count() == 1U);
    CHECK(stage.result()->bytes_consumed() == schemas[0U].size() + schemas[1U].size());
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    std::vector<goldsrc::DeltaDescriptionStageEventType> events;
    while (auto event = stage.poll_event()) {
        events.push_back(event->type);
        CHECK(event->occurred_at == epoch + 2ms);
    }
    CHECK(events == std::vector<goldsrc::DeltaDescriptionStageEventType>{
                        goldsrc::DeltaDescriptionStageEventType::delta_schema_decoded,
                        goldsrc::DeltaDescriptionStageEventType::delta_schema_decoded,
                        goldsrc::DeltaDescriptionStageEventType::delta_registry_ready,
                        goldsrc::DeltaDescriptionStageEventType::post_delta_boundary,
                    });

    REQUIRE(transport.sent.size() == 2U);
    const auto acknowledgement = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent[1U].payload);
    REQUIRE(acknowledgement);
    REQUIRE(acknowledgement.packet);
    CHECK(acknowledgement.packet->payload.size() == 8U);
    CHECK(std::ranges::all_of(
        acknowledgement.packet->payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
    CHECK(std::ranges::count_if(
              trace,
              [](const goldsrc::DeltaDescriptionTraceEvent& event) {
                  return event.classification ==
                      goldsrc::DeltaDescriptionTraceClassification::
                          delta_registry_ready;
              }) == 1);

    const auto sends = transport.sent.size();
    const auto receives = transport.receive_calls;
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.sent.size() == sends);
    CHECK(transport.receive_calls == receives);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Delta description stage backpressure publishes no partial registry",
          "[goldsrc][delta][stage][backpressure][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'240U);
    const auto epoch = goldsrc::DeltaDescriptionStageTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_events = 3U;
    std::size_t releases = 0U;
    goldsrc::DeltaDescriptionStage stage{transport, remote, config};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    deliver(
        stage,
        transport,
        remote,
        request,
        fixture::service_payload(two_schemas()),
        epoch + 2ms);

    CHECK(stage.state() == goldsrc::DeltaDescriptionStageState::backpressure);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::DeltaDescriptionStageErrorCode::event_backpressure);
    CHECK_FALSE(stage.result());
    CHECK_FALSE(stage.poll_event());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Delta description stage rejects duplicate schema without publication",
          "[goldsrc][delta][stage][duplicate][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'240U);
    const auto epoch = goldsrc::DeltaDescriptionStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::DeltaDescriptionStage stage{transport, remote, test_config()};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    const auto duplicate = fixture::schema("alpha_t", fixture::kSchemaAlphaFields);
    deliver(
        stage,
        transport,
        remote,
        request,
        fixture::service_payload(
            std::vector<std::vector<std::byte>>{duplicate, duplicate}),
        epoch + 2ms);

    CHECK(stage.state() == goldsrc::DeltaDescriptionStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::DeltaDescriptionStageErrorCode::delta_stream_decode_failed);
    CHECK(stage.error()->stream_code ==
          goldsrc::DeltaDescriptionStreamErrorCode::registry_failure);
    CHECK(stage.error()->registry_code ==
          goldsrc::DeltaRegistryErrorCode::duplicate_schema_name);
    CHECK_FALSE(stage.result());
    CHECK_FALSE(stage.poll_event());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
    CHECK(transport.sent.size() == 2U);
}

TEST_CASE("Delta description stage maps timeout cancellation and network failure",
          "[goldsrc][delta][stage][terminal][lifetime]")
{
    const auto remote = network::NetworkAddress::loopback(27'240U);
    const auto epoch = goldsrc::DeltaDescriptionStageTimePoint{} + 1s;

    SECTION("timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::DeltaDescriptionStage stage{transport, remote, test_config()};
        static_cast<void>(start_and_send_request(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases)));
        stage.update(epoch + 51ms);
        CHECK(stage.state() == goldsrc::DeltaDescriptionStageState::timed_out);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("cancellation")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::DeltaDescriptionStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(
            epoch,
            transport.local,
            std::make_unique<CountingLifetime>(releases)));
        stage.cancel(epoch + 1ms);
        CHECK(stage.state() == goldsrc::DeltaDescriptionStageState::cancelled);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("driver receive error")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::DeltaDescriptionStage stage{transport, remote, test_config()};
        static_cast<void>(start_and_send_request(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases)));
        transport.queue_error();
        stage.update(epoch + 2ms);
        CHECK(stage.state() == goldsrc::DeltaDescriptionStageState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::receive_failed);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }
}

} // namespace
