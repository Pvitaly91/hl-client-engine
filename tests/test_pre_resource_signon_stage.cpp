#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/pre_resource_signon_stage.hpp>
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
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
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
        ++local_address_calls;
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
        return network::DatagramSendResult{
            network::DatagramSendStatus::sent,
            {},
        };
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        std::size_t) override
    {
        ++receive_calls;
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

    void queue(
        const network::NetworkAddress source,
        std::vector<std::byte> payload)
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
            "synthetic pre-resource receive failure",
        });
    }

    network::NetworkAddress local{network::NetworkAddress::loopback(30'500U)};
    mutable std::size_t local_address_calls{0U};
    std::size_t receive_calls{0U};
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
};

class CountingLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

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

void append_u32_le(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void append_string(std::vector<std::byte>& bytes, const std::string_view value)
{
    const auto source = std::as_bytes(std::span{value.data(), value.size()});
    bytes.insert(bytes.end(), source.begin(), source.end());
    bytes.push_back(std::byte{0U});
}

[[nodiscard]] std::vector<std::byte> synthetic_server_info_body(
    const std::uint8_t maximum_clients = 8U,
    const std::string_view map_path = "maps/test_alpha.bsp")
{
    std::vector<std::byte> bytes;
    append_u32_le(bytes, 48U);
    append_u32_le(bytes, 0x1234'5678U);
    append_u32_le(bytes, 0xdead'beefU);
    for (std::uint8_t value = 0U; value < 16U; ++value) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    bytes.push_back(static_cast<std::byte>(maximum_clients));
    bytes.push_back(std::byte{0U}); // opaque slot candidate
    bytes.push_back(maximum_clients == 1U ? std::byte{0U} : std::byte{1U});
    append_string(bytes, "sample");
    append_string(bytes, "Local Test");
    append_string(bytes, map_path);
    append_string(bytes, "alpha beta");
    bytes.push_back(std::byte{0U});
    return bytes;
}

[[nodiscard]] std::vector<std::byte> semantic_payload(
    const bool include_leading_text = true,
    const std::uint8_t maximum_clients = 8U,
    const std::string_view map_path = "maps/test_alpha.bsp")
{
    std::vector<std::byte> bytes;
    if (include_leading_text) {
        bytes.push_back(std::byte{8U});
        bytes.push_back(std::byte{'x'});
        bytes.push_back(std::byte{0U});
    }
    bytes.push_back(std::byte{11U});
    const auto body = synthetic_server_info_body(maximum_clients, map_path);
    bytes.insert(bytes.end(), body.begin(), body.end());
    bytes.push_back(
        static_cast<std::byte>(goldsrc::kPreResourceSimpleControlOpcode));
    bytes.push_back(std::byte{0U});
    bytes.push_back(std::byte{0U});
    bytes.push_back(
        static_cast<std::byte>(goldsrc::kPreResourceComplexBoundaryOpcode));
    bytes.push_back(std::byte{0xa5U});
    bytes.push_back(std::byte{0x5aU});
    return bytes;
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

[[nodiscard]] goldsrc::PreResourceSignonConfig test_config()
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

[[nodiscard]] goldsrc::ClientToServerNetchanPacket start_and_send_request(
    goldsrc::PreResourceSignonStage& stage,
    FakeTransport& transport,
    const goldsrc::PreResourceSignonTimePoint epoch,
    std::unique_ptr<goldsrc::INetchanDriverLifetime> lifetime = {})
{
    REQUIRE(stage.start(epoch, transport.local, std::move(lifetime)));
    CHECK(stage.state() ==
          goldsrc::PreResourceSignonStageState::waiting_for_initial_boundary);
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
    CHECK(std::ranges::all_of(
        std::span<const std::byte>{decoded.packet->payload}.subspan(5U),
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
    return std::move(*decoded.packet);
}

void deliver_semantic(
    goldsrc::PreResourceSignonStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::ClientToServerNetchanPacket& request,
    std::vector<std::byte> semantic,
    const goldsrc::PreResourceSignonTimePoint now)
{
    transport.queue(
        remote,
        server_packet(
            request.header.sequence.sequence.value(),
            service_envelope(semantic)));
    stage.update(now);
}

TEST_CASE("Pre-resource stage owns one driver through typed boundary success",
          "[goldsrc][signon][pre-resource][stage][success][lifetime]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    CHECK(releases == 0U);

    const auto semantic = semantic_payload();
    deliver_semantic(stage, transport, remote, request, semantic, epoch + 2ms);

    REQUIRE(stage.state() ==
            goldsrc::PreResourceSignonStageState::pre_resource_boundary_reached);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->server_info().protocol_version() ==
          goldsrc::ProtocolVersion::goldsrc_48);
    CHECK(stage.result()->server_info().maximum_clients().value() == 8U);
    CHECK(stage.result()->server_info().multi_client_mode());
    CHECK(stage.result()->server_info().game_directory() == "sample");
    CHECK(stage.result()->server_info().server_label() == "Local Test");
    CHECK(stage.result()->server_info().map_file_path() ==
          "maps/test_alpha.bsp");
    REQUIRE(stage.result()->controls().size() == 1U);
    CHECK(stage.result()->controls().front().opcode() ==
          goldsrc::kPreResourceSimpleControlOpcode);
    CHECK(stage.result()->boundary().opcode() ==
          goldsrc::kPreResourceComplexBoundaryOpcode);
    CHECK(stage.result()->boundary().byte_offset() == 88U);
    CHECK(stage.result()->boundary().remaining_byte_count() == 2U);
    CHECK(stage.result()->boundary().direction() ==
          goldsrc::ResourcePhaseBoundaryDirection::server_message);
    CHECK(stage.result()->source_payload().payload_size() == semantic.size());
    CHECK(stage.result()->source_payload().initial_boundary_offset() == 3U);
    CHECK(stage.result()->source_payload().server_info_body_offset() == 4U);
    CHECK(stage.result()->source_payload().server_info_body_size() == 81U);
    CHECK_FALSE(stage.result()->source_payload().reassembled());
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.transmitted_packet_count() == 2U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    std::vector<goldsrc::PreResourceSignonEventType> events;
    while (auto event = stage.poll_event()) {
        events.push_back(event->type);
        CHECK(event->occurred_at == epoch + 2ms);
    }
    CHECK(events == std::vector<goldsrc::PreResourceSignonEventType>{
                        goldsrc::PreResourceSignonEventType::server_info_ready,
                        goldsrc::PreResourceSignonEventType::pre_resource_control,
                        goldsrc::PreResourceSignonEventType::resource_phase_boundary,
                    });

    const auto sent_at_boundary = transport.sent.size();
    const auto receives_at_boundary = transport.receive_calls;
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.sent.size() == sent_at_boundary);
    CHECK(transport.receive_calls == receives_at_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Pre-resource stage uses the retained offset-zero boundary cursor",
          "[goldsrc][signon][pre-resource][stage][offset-zero][max-clients]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
    const auto request = start_and_send_request(stage, transport, epoch);
    deliver_semantic(
        stage,
        transport,
        remote,
        request,
        semantic_payload(false, 1U),
        epoch + 2ms);

    REQUIRE(stage.result());
    CHECK(stage.result()->server_info().maximum_clients().value() == 1U);
    CHECK_FALSE(stage.result()->server_info().multi_client_mode());
    CHECK(stage.result()->source_payload().initial_boundary_offset() == 0U);
    CHECK(stage.result()->source_payload().server_info_body_offset() == 1U);
    CHECK(stage.result()->boundary().byte_offset() == 85U);
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Pre-resource decode failure finalizes retained ownership without state",
          "[goldsrc][signon][pre-resource][stage][failure][transaction][lifetime]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    auto malformed = semantic_payload(false, 1U);
    malformed[1U] = std::byte{47U};
    deliver_semantic(
        stage,
        transport,
        remote,
        request,
        std::move(malformed),
        epoch + 2ms);

    CHECK(stage.state() == goldsrc::PreResourceSignonStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PreResourceSignonErrorCode::pre_resource_decode_failed);
    CHECK(stage.error()->service_code ==
          goldsrc::PreResourceServiceErrorCode::server_info_decode_failed);
    CHECK(stage.error()->server_info_code ==
          goldsrc::ServerInfoErrorCode::unsupported_protocol);
    CHECK_FALSE(stage.result());
    CHECK_FALSE(stage.poll_event());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
    CHECK(transport.sent.size() == 2U);
}

TEST_CASE("Pre-resource event backpressure publishes no partial state or events",
          "[goldsrc][signon][pre-resource][stage][backpressure][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_events = 2U;
    std::size_t releases = 0U;
    goldsrc::PreResourceSignonStage stage{transport, remote, config};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    deliver_semantic(
        stage,
        transport,
        remote,
        request,
        semantic_payload(),
        epoch + 2ms);

    CHECK(stage.state() == goldsrc::PreResourceSignonStageState::backpressure);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PreResourceSignonErrorCode::event_backpressure);
    CHECK_FALSE(stage.result());
    CHECK(stage.pending_event_count() == 0U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Pre-resource stage maps driver receive failure and releases once",
          "[goldsrc][signon][pre-resource][stage][network-error][lifetime]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
    static_cast<void>(start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases)));
    CHECK(releases == 0U);

    transport.queue_receive_error();
    stage.update(epoch + 2ms);

    CHECK(stage.state() ==
          goldsrc::PreResourceSignonStageState::network_error);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PreResourceSignonErrorCode::initial_signon_failed);
    CHECK(stage.error()->initial_signon_code ==
          goldsrc::InitialSignonErrorCode::driver_network_error);
    CHECK(stage.error()->driver_code ==
          goldsrc::NetchanDriverErrorCode::receive_failed);
    CHECK_FALSE(stage.poll_event());
    CHECK(stage.transmitted_packet_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    const auto receives = transport.receive_calls;
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.receive_calls == receives);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Pre-resource stage maps unsupported post-server-info opcode atomically",
          "[goldsrc][signon][pre-resource][stage][unsupported][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    auto unsupported = semantic_payload();
    REQUIRE(unsupported.size() >= 6U);
    unsupported[unsupported.size() - 6U] = std::byte{55U};
    deliver_semantic(
        stage,
        transport,
        remote,
        request,
        std::move(unsupported),
        epoch + 2ms);

    CHECK(stage.state() ==
          goldsrc::PreResourceSignonStageState::unsupported_message);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PreResourceSignonErrorCode::unsupported_message);
    CHECK(stage.error()->service_code ==
          goldsrc::PreResourceServiceErrorCode::
              unsupported_post_server_info_opcode);
    CHECK_FALSE(stage.poll_event());
    CHECK(stage.transmitted_packet_count() == 2U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    const auto sends = transport.sent.size();
    const auto receives = transport.receive_calls;
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.sent.size() == sends);
    CHECK(transport.receive_calls == receives);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Pre-resource timeout cancellation and destruction release once",
          "[goldsrc][signon][pre-resource][stage][timeout][cancel][destructor]")
{
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;

    SECTION("timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
        static_cast<void>(start_and_send_request(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases)));
        stage.update(epoch + 51ms);
        CHECK(stage.state() == goldsrc::PreResourceSignonStageState::timed_out);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
        stage.update(epoch + 52ms);
        stage.cancel(epoch + 53ms);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("cancellation")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(
            epoch,
            transport.local,
            std::make_unique<CountingLifetime>(releases)));
        stage.cancel(epoch + 1ms);
        CHECK(stage.state() == goldsrc::PreResourceSignonStageState::cancelled);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
        stage.cancel(epoch + 2ms);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("active destruction")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        {
            goldsrc::PreResourceSignonStage stage{
                transport,
                remote,
                test_config()};
            REQUIRE(stage.start(
                epoch,
                transport.local,
                std::make_unique<CountingLifetime>(releases)));
            CHECK(releases == 0U);
        }
        CHECK(releases == 1U);
    }
}

TEST_CASE("Invalid pre-resource configuration fails before network mutation",
          "[goldsrc][signon][pre-resource][stage][configuration][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_events = 0U;
    std::size_t releases = 0U;
    goldsrc::PreResourceSignonStage stage{transport, remote, config};
    CHECK_FALSE(stage.start(
        epoch,
        transport.local,
        std::make_unique<CountingLifetime>(releases)));
    CHECK(stage.state() == goldsrc::PreResourceSignonStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PreResourceSignonErrorCode::invalid_configuration);
    CHECK(transport.sent.empty());
    CHECK(transport.receive_calls == 0U);
    CHECK(stage.cleanup_count() == 0U);
    CHECK(releases == 1U);
}

TEST_CASE("Nested initial trace cannot reenter composite mutators",
          "[goldsrc][signon][pre-resource][stage][trace][reentry][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    goldsrc::PreResourceSignonStage* stage_pointer = nullptr;
    std::size_t callbacks = 0U;
    bool any_reentrant_start_succeeded = false;
    goldsrc::PreResourceSignonStage stage{
        transport,
        remote,
        test_config(),
        [&](const goldsrc::InitialSignonTraceEvent&) {
            ++callbacks;
            if (stage_pointer != nullptr) {
                any_reentrant_start_succeeded =
                    stage_pointer->start(epoch + 10ms, transport.local) ||
                    any_reentrant_start_succeeded;
                stage_pointer->update(epoch + 10ms);
                stage_pointer->cancel(epoch + 10ms);
            }
        }};
    stage_pointer = &stage;

    const auto request = start_and_send_request(stage, transport, epoch);
    CHECK(callbacks >= 2U);
    CHECK_FALSE(any_reentrant_start_succeeded);
    CHECK(stage.state() ==
          goldsrc::PreResourceSignonStageState::waiting_for_initial_boundary);
    CHECK_FALSE(stage.error());
    deliver_semantic(
        stage,
        transport,
        remote,
        request,
        semantic_payload(),
        epoch + 2ms);
    CHECK(stage.state() ==
          goldsrc::PreResourceSignonStageState::pre_resource_boundary_reached);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(transport.sent.size() == 2U);
}

TEST_CASE("Pre-resource trace is numeric metadata only and exception isolated",
          "[goldsrc][signon][pre-resource][stage][trace][redaction][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    std::vector<goldsrc::PreResourceSignonTraceEvent> trace;
    goldsrc::PreResourceSignonStage stage{
        transport,
        remote,
        test_config(),
        {},
        [&](const goldsrc::PreResourceSignonTraceEvent& event) {
            trace.push_back(event);
            throw 7;
        }};
    const auto request = start_and_send_request(stage, transport, epoch);
    deliver_semantic(
        stage,
        transport,
        remote,
        request,
        semantic_payload(),
        epoch + 2ms);

    REQUIRE(stage.result());
    CHECK(std::ranges::all_of(
        trace,
        [&](const goldsrc::PreResourceSignonTraceEvent& event) {
            return event.endpoint == remote;
        }));
    const auto server_info_trace = std::ranges::find_if(
        trace,
        [](const goldsrc::PreResourceSignonTraceEvent& event) {
            return event.classification ==
                goldsrc::PreResourceSignonTraceClassification::server_info_ready;
        });
    REQUIRE(server_info_trace != trace.end());
    CHECK(server_info_trace->protocol_version == 48U);
    CHECK(server_info_trace->maximum_clients == 8U);
    CHECK(server_info_trace->multi_client_mode == true);
}

TEST_CASE("Wrong endpoint and duplicate terminal batch cannot publish twice",
          "[goldsrc][signon][pre-resource][stage][endpoint][duplicate][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'230U);
    const auto rogue = network::NetworkAddress::loopback(27'231U);
    const auto epoch = goldsrc::PreResourceSignonTimePoint{} + 1s;
    goldsrc::PreResourceSignonStage stage{transport, remote, test_config()};
    const auto request = start_and_send_request(stage, transport, epoch);
    const auto response = server_packet(
        request.header.sequence.sequence.value(),
        service_envelope(semantic_payload()));

    transport.queue(rogue, response);
    stage.update(epoch + 2ms);
    CHECK(stage.state() ==
          goldsrc::PreResourceSignonStageState::waiting_for_initial_boundary);
    CHECK_FALSE(stage.result());

    transport.queue(remote, response);
    stage.update(epoch + 3ms);
    REQUIRE(stage.result());
    CHECK(stage.pending_event_count() == 3U);
    const auto sends_at_boundary = transport.sent.size();
    transport.queue(remote, response);
    stage.update(epoch + 4ms);
    CHECK(stage.pending_event_count() == 3U);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(transport.incoming.size() == 1U);
}

} // namespace
