#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/resource_transition_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
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
        return {local, {}};
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

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        std::size_t) override
    {
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

    network::NetworkAddress local{network::NetworkAddress::loopback(30'701U)};
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
    const std::span<const std::byte> semantic_payload)
{
    REQUIRE_FALSE(semantic_payload.empty());
    REQUIRE(semantic_payload.size() <=
            (std::numeric_limits<unsigned int>::max)());
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

[[nodiscard]] std::vector<std::byte> normal_fragment_packet(
    const std::uint32_t packet_sequence,
    const bool reliable,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    std::vector<std::byte> fragment_payload)
{
    REQUIRE(fragment_index >= 1U);
    REQUIRE(fragment_index <= fragment_count);
    REQUIRE_FALSE(fragment_payload.empty());
    REQUIRE(fragment_payload.size() <=
            (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_size = fragment_payload.size();
    goldsrc::NetchanFragmentSlots fragments;
    fragments[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        (static_cast<std::uint32_t>(fragment_index) << 16U) |
            fragment_count,
        0U,
        static_cast<std::uint16_t>(fragment_size),
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{reliable, true},
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
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> secondary_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement)
{
    goldsrc::NetchanFragmentSlots fragments;
    fragments[1U] = goldsrc::NetchanFragmentDescriptor{
        1U,
        (1U << 16U) | 1U,
        0U,
        1U,
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
        std::vector<std::byte>{std::byte{0x41U}},
        1U,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::ResourceTransitionStageConfig test_config()
{
    goldsrc::ResourceTransitionStageConfig config;
    auto& driver = config.user_info.movement_environment.delta.pre_resource
                       .initial_signon.driver;
    driver.channel_inactivity_timeout = 100ms;
    driver.fragment_transfer_timeout = 100ms;
    driver.maximum_datagrams_per_update = 16U;
    driver.maximum_outgoing_packets_per_update = 8U;
    driver.maximum_events = 64U;
    config.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_events = 64U;
    config.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_driver_events_per_update = 64U;
    config.user_info.movement_environment.delta.pre_resource.maximum_events = 64U;
    config.user_info.movement_environment.delta.maximum_events = 64U;
    config.user_info.movement_environment.maximum_events = 64U;
    config.user_info.maximum_stage_events = 64U;
    config.maximum_stage_events = 64U;
    config.maximum_driver_events_per_update = 64U;
    return config;
}

[[nodiscard]] std::vector<std::vector<std::byte>> schemas()
{
    return {
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("bravo_t", delta_fixture::kSchemaBravoFields),
    };
}

[[nodiscard]] std::vector<std::byte> first_semantic_payload(
    const bool multiple_user_info = false)
{
    std::vector<std::byte> post_delta;
    move_fixture::append_move_vars_body(post_delta);
    move_fixture::append_confirmed_controls(post_delta);
    if (multiple_user_info) {
        const auto first = user_fixture::make_message(
            1U,
            0x11223344U,
            "\\name\\First\\model\\scientist");
        post_delta.insert(post_delta.end(), first.begin(), first.end());
        const auto second = user_fixture::make_message(
            2U,
            0x55667788U,
            "\\name\\Second\\model\\barney");
        post_delta.insert(post_delta.end(), second.begin(), second.end());
    } else {
        post_delta.insert(
            post_delta.end(),
            user_fixture::kExactUserInfoMessage.begin(),
            user_fixture::kExactUserInfoMessage.end());
    }
    return delta_fixture::service_payload(
        schemas(),
        goldsrc::kMoveVarsOpcode,
        post_delta);
}

[[nodiscard]] std::vector<std::byte> second_semantic_payload(
    const std::size_t suffix_size = 1U)
{
    std::vector<std::byte> payload{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{43U},
    };
    std::uint32_t state = 0x9e3779b9U;
    for (std::size_t index = 0U; index < suffix_size; ++index) {
        // Deterministic xorshift bytes keep the BZ2 envelope above the
        // fragment threshold without modelling resource-entry semantics.
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        payload.push_back(static_cast<std::byte>(state & 0xffU));
    }
    return payload;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket decode_sent(
    const SentDatagram& datagram)
{
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    return *decoded.packet;
}

[[nodiscard]] std::vector<std::byte> with_resource_location(
    std::vector<std::byte> payload,
    const std::string_view location)
{
    std::vector<std::byte> message{std::byte{56U}};
    for (const char character : location) {
        message.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    message.push_back(std::byte{0U});
    payload.insert(
        payload.begin() + static_cast<std::ptrdiff_t>(
            goldsrc::kResourceTransitionControlMessageSize),
        message.begin(), message.end());
    return payload;
}

struct DrivenTransition {
    goldsrc::ClientToServerNetchanPacket initial_request;
    goldsrc::ClientToServerNetchanPacket transition_request;
};

[[nodiscard]] DrivenTransition drive_to_transition_request(
    goldsrc::ResourceTransitionStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::ResourceTransitionStageTimePoint epoch,
    const bool multiple_user_info = false,
    std::unique_ptr<goldsrc::INetchanDriverLifetime> lifetime = {})
{
    REQUIRE(stage.start(epoch, transport.local, std::move(lifetime)));
    stage.update(epoch + 1ms);
    REQUIRE(transport.sent.size() == 1U);
    const auto initial = decode_sent(transport.sent.front());

    transport.queue(
        remote,
        server_packet(
            1U,
            true,
            initial.header.sequence.sequence.value(),
            initial.header.sequence.flags.reliable,
            service_envelope(first_semantic_payload(multiple_user_info))));
    stage.update(epoch + 2ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::waiting_for_request_transmit);
    CHECK(stage.initial_request_queue_count() == 1U);
    CHECK(stage.transition_request_queue_count() == 1U);

    stage.update(epoch + 3ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
    REQUIRE(transport.sent.size() >= 3U);
    const auto transition = decode_sent(transport.sent.back());
    REQUIRE(transition.payload.size() == goldsrc::kResourceTransitionRequestSize);
    const std::array exact{
        std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
        std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
        std::byte{0U},
    };
    CHECK(std::ranges::equal(transition.payload, exact));
    return DrivenTransition{initial, transition};
}

void deliver_second_transfer(
    goldsrc::ResourceTransitionStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const DrivenTransition& driven,
    const goldsrc::ResourceTransitionStageTimePoint now,
    const std::size_t suffix_size = 1U)
{
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(second_semantic_payload(suffix_size))));
    stage.update(now);
}

void run_baseline(const std::size_t run, const bool multiple_user_info = false)
{
    FakeTransport transport;
    transport.local = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(30'701U + run));
    const auto remote = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(27'401U + run));
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::ResourceTransitionStage stage{
        transport,
        remote,
        test_config()};
    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        multiple_user_info,
        std::make_unique<CountingLifetime>(releases));
    CHECK(releases == 0U);

    deliver_second_transfer(stage, transport, remote, driven, epoch + 4ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::
                neutral_opcode43_boundary_reached);
    REQUIRE(stage.terminal());
    REQUIRE_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->user_info().message_count() ==
          (multiple_user_info ? 2U : 1U));
    CHECK(stage.result()->control().body_bytes() == 8U);
    CHECK(stage.result()->boundary().opcode() == 43U);
    CHECK(stage.result()->boundary().byte_offset() == 9U);
    CHECK(stage.result()->boundary().remaining_byte_count() == 1U);
    CHECK(stage.result()->source_payload().decompressed_byte_count() == 11U);
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(stage.transition_request_transmitted());
    CHECK(stage.transition_request_acknowledged());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    const auto sends_at_boundary = transport.sent.size();
    stage.update(epoch + 20ms);
    stage.cancel(epoch + 30ms);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

void run_dropped_request(const std::size_t run)
{
    FakeTransport transport;
    transport.local = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(31'001U + run));
    const auto remote = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(27'701U + run));
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        false,
        std::make_unique<CountingLifetime>(releases));
    const auto envelope = service_envelope(second_semantic_payload(1'200U));
    REQUIRE(envelope.size() > goldsrc::kStockProtocol48NormalFragmentChunkSize);
    REQUIRE(envelope.size() <=
            2U * goldsrc::kStockProtocol48NormalFragmentChunkSize);
    const auto final_offset = goldsrc::kStockProtocol48NormalFragmentChunkSize;

    // Cover the numeric request sequence with the old reliable generation.
    // The driver, not the stage, owns the exact retransmission.
    transport.queue(
        remote,
        normal_fragment_packet(
            2U,
            true,
            driven.transition_request.header.sequence.sequence.value(),
            true,
            2U,
            2U,
            std::vector<std::byte>{
                envelope.begin() + static_cast<std::ptrdiff_t>(final_offset),
                envelope.end()}));
    stage.update(epoch + 4ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
    const auto covering_ack = decode_sent(transport.sent.back());

    transport.queue(
        remote,
        normal_fragment_packet(
            3U,
            true,
            covering_ack.header.sequence.sequence.value(),
            true,
            1U,
            2U,
            std::vector<std::byte>{
                envelope.begin(),
                envelope.begin() + static_cast<std::ptrdiff_t>(final_offset)}));
    stage.update(epoch + 5ms);

    std::size_t semantic_packets = 0U;
    for (const auto& sent : transport.sent) {
        const auto packet = decode_sent(sent);
        if (std::ranges::equal(
                packet.payload,
                driven.transition_request.payload)) {
            ++semantic_packets;
        }
    }
    CHECK(semantic_packets == 2U);
    CHECK(stage.transition_request_queue_count() == 1U);
    const auto retry = decode_sent(transport.sent.back());
    transport.queue(
        remote,
        server_packet(
            4U,
            false,
            retry.header.sequence.sequence.value(),
            false,
            {}));
    stage.update(epoch + 6ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::
                neutral_opcode43_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->boundary().remaining_byte_count() == 1'200U);
    CHECK(stage.transition_request_acknowledged());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
    const auto sends_at_boundary = transport.sent.size();
    stage.update(epoch + 7ms);
    stage.cancel(epoch + 8ms);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(releases == 1U);
}

void run_fragmented_second_transfer(const std::size_t run)
{
    FakeTransport transport;
    transport.local = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(31'301U + run));
    const auto remote = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(28'001U + run));
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    const auto envelope = service_envelope(second_semantic_payload(8'192U));
    REQUIRE(envelope.size() > goldsrc::kStockProtocol48NormalFragmentChunkSize);
    const auto count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(count_size >= 2U);
    REQUIRE(count_size <= (std::numeric_limits<std::uint16_t>::max)());
    const auto count = static_cast<std::uint16_t>(count_size);

    std::uint32_t server_sequence = 2U;
    auto now = epoch + 4ms;
    for (std::uint16_t reverse = count; reverse >= 1U; --reverse) {
        const auto offset = static_cast<std::size_t>(reverse - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        transport.queue(
            remote,
            normal_fragment_packet(
                server_sequence,
                true,
                driven.transition_request.header.sequence.sequence.value(),
                false,
                reverse,
                count,
                std::vector<std::byte>{
                    envelope.begin() + static_cast<std::ptrdiff_t>(offset),
                    envelope.begin() + static_cast<std::ptrdiff_t>(offset + length)}));
        stage.update(now);
        ++server_sequence;
        now += 1ms;
        if (reverse == 1U) {
            break;
        }
    }

    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::
                neutral_opcode43_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->source_payload().reassembled());
    CHECK(stage.result()->source_payload().decompressed());
    CHECK(stage.result()->source_payload().decompressed_byte_count() == 8'202U);
    CHECK(stage.result()->boundary().remaining_byte_count() == 8'192U);
    CHECK(stage.transition_request_queue_count() == 1U);
}

[[nodiscard]] std::size_t count_exact_transition_packets(
    const FakeTransport& transport)
{
    const std::array exact{
        std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
        std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
        std::byte{0U},
    };
    return static_cast<std::size_t>(std::ranges::count_if(
        transport.sent,
        [&](const SentDatagram& datagram) {
            const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
                datagram.payload);
            return decoded && decoded.packet &&
                   std::ranges::equal(decoded.packet->payload, exact);
        }));
}

TEST_CASE("Resource-transition stage preserves one driver and stops before opcode-43 body",
          "[goldsrc][resource-transition][stage][success][lifetime][security]")
{
    run_baseline(0U);
}

TEST_CASE("Resource-transition dispatcher decodes svc_nop before exact opcode 45",
          "[goldsrc][resource-transition][stage][dispatch][nop][cursor][regression]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'420U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    auto semantic = second_semantic_payload();
    semantic.insert(semantic.begin(), std::byte{1U});
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(semantic)));
    stage.update(epoch + 4ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::
                neutral_opcode43_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->control().source_message_offset() == 1U);
    CHECK(stage.result()->boundary().byte_offset() == 10U);
    CHECK(stage.result()->boundary().source_payload_size() == semantic.size());
    bool observed_nop = false;
    while (const auto event = stage.poll_event()) {
        if (event->type ==
            goldsrc::ResourceTransitionStageEventType::
                intermediate_message_decoded) {
            observed_nop = true;
            CHECK(event->opcode == 1U);
            CHECK(event->byte_offset == 0U);
            CHECK(event->byte_count == 1U);
        }
    }
    CHECK(observed_nop);
}

TEST_CASE("Resource-transition stage preserves the exact boundary after compressed FastDL metadata",
          "[goldsrc][resource-transition][stage][fastdl][bzip2][cursor][regression]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'430U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    // '+' is byte 43 and '8' is byte 56: neither is a service boundary
    // inside the NUL-terminated location string.
    constexpr std::string_view location{"https://cdn.invalid/hl/+8/"};
    auto semantic = with_resource_location(second_semantic_payload(), location);
    semantic.insert(semantic.begin(), std::byte{1U});
    const auto expected_boundary = 1U +
        goldsrc::kResourceTransitionControlMessageSize + location.size() + 2U;
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(semantic)));
    stage.update(epoch + 4ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::neutral_opcode43_boundary_reached);
    REQUIRE(stage.result());
    CHECK_FALSE(stage.error());
    CHECK(stage.result()->control().source_message_offset() == 1U);
    CHECK(stage.result()->control().body_bytes() == 8U);
    CHECK(stage.result()->boundary().opcode() == 43U);
    CHECK(stage.result()->boundary().byte_offset() == expected_boundary);
    CHECK(stage.result()->boundary().remaining_byte_count() == 1U);
    CHECK(stage.result()->boundary().source_payload_size() == semantic.size());
    CHECK(stage.result()->source_payload().decompressed());
    CHECK(stage.result()->source_payload().decompressed_byte_count() == semantic.size());
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(stage.transition_request_acknowledged());
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Resource-transition benign controls keep their exact cursor and remain inert",
          "[goldsrc][resource-transition][stage][dispatch][public-server][security][no-stock]")
{
    const bool separate_payload = GENERATE(false, true);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'450U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
    // The query ID and string contain 45/43: neither is a boundary inside a body.
    const std::vector<std::byte> controls{
        std::byte{8U}, std::byte{'h'}, std::byte{0U},
        std::byte{9U}, std::byte{'q'}, std::byte{'u'}, std::byte{'i'}, std::byte{'t'}, std::byte{0U},
        std::byte{57U}, std::byte{'x'}, std::byte{45U}, std::byte{43U}, std::byte{0U},
        std::byte{58U}, std::byte{45U}, std::byte{43U}, std::byte{0U}, std::byte{0U},
        std::byte{'y'}, std::byte{45U}, std::byte{43U}, std::byte{0U}, std::byte{1U}};
    auto semantic = controls;
    if (!separate_payload) {
        const auto transition = second_semantic_payload();
        semantic.insert(semantic.end(), transition.begin(), transition.end());
    }
    transport.queue(remote, server_packet(2U, false,
        driven.transition_request.header.sequence.sequence.value(), false,
        service_envelope(semantic)));
    stage.update(epoch + 4ms);
    if (separate_payload) {
        REQUIRE(stage.state() == goldsrc::ResourceTransitionStageState::waiting_for_server_transfer);
        CHECK_FALSE(stage.result());
        transport.queue(remote, server_packet(3U, false,
            driven.transition_request.header.sequence.sequence.value(), false,
            service_envelope(second_semantic_payload())));
        stage.update(epoch + 5ms);
    }
    REQUIRE(stage.state() == goldsrc::ResourceTransitionStageState::neutral_opcode43_boundary_reached);
    REQUIRE(stage.result());
    const auto control_offset = separate_payload ? 0U : controls.size();
    CHECK(stage.result()->control().source_message_offset() == control_offset);
    CHECK(stage.result()->boundary().byte_offset() == control_offset + 9U);
    CHECK(stage.transition_request_queue_count() == 1U);
    std::vector<unsigned> observed;
    std::vector<std::size_t> offsets, lengths;
    while (const auto event = stage.poll_event()) {
        if (event->type == goldsrc::ResourceTransitionStageEventType::intermediate_message_decoded) {
            REQUIRE(event->opcode);
            observed.push_back(*event->opcode);
            offsets.push_back(event->byte_offset);
            lengths.push_back(event->byte_count);
        }
    }
    CHECK(observed == std::vector<unsigned>{8U, 9U, 57U, 58U, 1U});
    CHECK(offsets == std::vector<std::size_t>{0U, 3U, 9U, 14U, 23U});
    CHECK(lengths == std::vector<std::size_t>{3U, 6U, 5U, 9U, 1U});
    // Only the fixed initial/sendres messages and transport ACKs can be sent.
    for (const auto& sent : transport.sent) {
        const auto packet = decode_sent(sent);
        if (packet.payload.empty()) continue;
        const auto opcode = std::to_integer<unsigned>(packet.payload.front());
        CHECK(opcode != 10U); // clc_cvarvalue
        CHECK(opcode != 11U); // clc_cvarvalue2
    }
}

TEST_CASE("Resource-transition retains raced userinfo without scanning into identity bytes",
          "[goldsrc][resource-transition][stage][userinfo][public-server]")
{
    const bool pre_ack = GENERATE(false, true);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28468U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
    const auto first = user_fixture::make_message(2U, 123U, "\\name\\Old-+\\model\\gman");
    const auto latest = user_fixture::make_message(2U, 123U, "\\name\\Newest-+\\model\\barney");
    auto combined = latest;
    const auto boundary = second_semantic_payload();
    combined.insert(combined.end(), boundary.begin(), boundary.end());
    if (pre_ack) {
        transport.queue(remote, server_packet(2U, true,
            driven.transition_request.header.sequence.sequence.value(), true, service_envelope(first)));
        stage.update(epoch + 4ms);
        REQUIRE(stage.state() == goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
        CHECK_FALSE(stage.result());
        const auto ack = decode_sent(transport.sent.back()).header.sequence.sequence.value();
        REQUIRE(ack > driven.transition_request.header.sequence.sequence.value());
        transport.queue(remote, server_packet(3U, false, ack, false, service_envelope(combined)));
        stage.update(epoch + 5ms);
        stage.update(epoch + 6ms);
    } else {
        combined.insert(combined.begin(), first.begin(), first.end());
        transport.queue(remote, server_packet(2U, false,
            driven.transition_request.header.sequence.sequence.value(), false, service_envelope(combined)));
        stage.update(epoch + 4ms);
    }
    INFO((stage.error() ? stage.error()->context : std::string{}));
    REQUIRE(stage.result());
    const auto& update = stage.result()->user_info_updates()[2U];
    REQUIRE(update);
    REQUIRE(update->identity);
    CHECK(update->identity->player_display_name() == "Newest-+");
    CHECK(stage.result()->control().source_message_offset() == latest.size() + (pre_ack ? 0U : first.size()));
    CHECK(stage.transition_request_queue_count() == 1U);
}

TEST_CASE("Resource-transition malformed userinfo cannot resync at a later resource boundary",
          "[goldsrc][resource-transition][stage][userinfo][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28469U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
    auto body = user_fixture::make_message(32U, 123U, "\\name\\Bad-+");
    const auto boundary = second_semantic_payload();
    body.insert(body.end(), boundary.begin(), boundary.end());
    transport.queue(remote, server_packet(2U, false,
        driven.transition_request.header.sequence.sequence.value(), false, service_envelope(body)));
    stage.update(epoch + 4ms);
    REQUIRE(stage.error());
    CHECK_FALSE(stage.result());
    CHECK(stage.error()->code == goldsrc::ResourceTransitionStageErrorCode::intermediate_message_decode_failed);
    CHECK(stage.error()->intermediate_control_code == goldsrc::RuntimeControlDecodeErrorCode::user_info_update_failed);
    REQUIRE(stage.error()->failure_metadata);
    CHECK(stage.error()->failure_metadata->actual_opcode == 13U);
    CHECK(stage.error()->failure_metadata->cursor_byte_offset == 0U);
}

TEST_CASE("Resource-transition queries remain bounded and malformed bodies never resync",
          "[goldsrc][resource-transition][stage][dispatch][public-server][security][no-stock]")
{
    const auto opcode = GENERATE(8U, 9U, 57U, 58U);
    std::vector<std::byte> control{static_cast<std::byte>(opcode)};
    if (opcode == 58U) {
        control.insert(control.end(), {std::byte{45U}, std::byte{43U}, std::byte{0U}, std::byte{0U}});
    }
    control.insert(control.end(), {std::byte{'x'}, std::byte{45U}, std::byte{43U}, std::byte{0U}});
    for (std::size_t size = 1U; size < control.size(); ++size) {
        INFO("opcode " << opcode << " prefix " << size);
        auto prefix = control;
        prefix.resize(size);
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'451U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
        transport.queue(remote, server_packet(2U, false,
            driven.transition_request.header.sequence.sequence.value(), false,
            service_envelope(prefix)));
        stage.update(epoch + 4ms);
        CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->intermediate_control_code == goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
        REQUIRE(stage.error()->failure_metadata);
        CHECK(stage.error()->failure_metadata->actual_opcode == opcode);
        CHECK(stage.error()->failure_metadata->cursor_byte_offset == 0U);
        while (const auto event = stage.poll_event()) {
            CHECK(event->type != goldsrc::ResourceTransitionStageEventType::transition_control_decoded);
            CHECK(event->type != goldsrc::ResourceTransitionStageEventType::intermediate_message_decoded);
        }
    }
}

TEST_CASE("Resource-transition still rejects unrelated decoder-validated controls",
          "[goldsrc][resource-transition][stage][dispatch][public-server][security][no-stock]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'452U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
    auto semantic = second_semantic_payload();
    semantic.insert(semantic.begin(), {std::byte{56U}, std::byte{0U}});
    transport.queue(remote, server_packet(2U, false,
        driven.transition_request.header.sequence.sequence.value(), false,
        service_envelope(semantic)));
    stage.update(epoch + 4ms);
    CHECK(stage.state() == goldsrc::ResourceTransitionStageState::unsupported_message);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceTransitionStageErrorCode::unexpected_intermediate_message);
    REQUIRE(stage.error()->failure_metadata);
    CHECK(stage.error()->failure_metadata->actual_opcode == 56U);
}

TEST_CASE("Resource-transition queries enforce the runtime decoder string cap",
          "[goldsrc][resource-transition][stage][dispatch][public-server][limits][no-stock]")
{
    const auto opcode = GENERATE(57U, 58U);
    const bool overlong = GENERATE(false, true);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'453U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
    std::vector<std::byte> semantic{static_cast<std::byte>(opcode)};
    if (opcode == 58U) semantic.insert(semantic.end(), 4U, std::byte{0U});
    semantic.insert(semantic.end(), overlong ? 256U : 255U, std::byte{'x'});
    semantic.push_back(std::byte{0U});
    const auto expected_offset = semantic.size();
    const auto transition = second_semantic_payload();
    semantic.insert(semantic.end(), transition.begin(), transition.end());
    transport.queue(remote, server_packet(2U, false,
        driven.transition_request.header.sequence.sequence.value(), false,
        service_envelope(semantic)));
    stage.update(epoch + 4ms);
    if (overlong) {
        CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->intermediate_control_code == goldsrc::RuntimeControlDecodeErrorCode::invalid_numeric_value);
    } else {
        REQUIRE(stage.result());
        CHECK(stage.result()->control().source_message_offset() == expected_offset);
    }
}

TEST_CASE("Resource-transition FastDL failures report boundary offsets without exposing location bytes",
          "[goldsrc][resource-transition][stage][fastdl][diagnostics][negative][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'431U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    constexpr std::string_view location{"https://cdn.invalid/hl/+8/"};
    auto semantic = with_resource_location(second_semantic_payload(), location);
    semantic.insert(semantic.begin(), std::byte{1U});
    const auto next_boundary = 1U +
        goldsrc::kResourceTransitionControlMessageSize + location.size() + 2U;

    auto expected_error = goldsrc::ResourceTransitionControlErrorCode::wrong_next_boundary_opcode;
    auto expected_parser_offset = next_boundary;
    auto expected_cursor_offset = next_boundary;
    std::optional<std::uint8_t> expected_opcode{55U};
    std::uint8_t expected_required_opcode = 43U;

    SECTION("unknown message after complete location is not skipped")
    {
        semantic.insert(
            semantic.begin() + static_cast<std::ptrdiff_t>(next_boundary),
            std::byte{55U});
    }
    SECTION("EOF after location is the exact missing boundary")
    {
        semantic.resize(next_boundary);
        expected_error = goldsrc::ResourceTransitionControlErrorCode::missing_next_boundary;
        expected_opcode.reset();
    }
    SECTION("opcode 43 without its body remains a boundary error")
    {
        semantic.resize(next_boundary + 1U);
        expected_error = goldsrc::ResourceTransitionControlErrorCode::truncated_next_boundary_body;
        expected_opcode = 43U;
    }
    SECTION("unterminated location never turns its raw bytes into an opcode")
    {
        semantic.resize(next_boundary - 1U);
        expected_error = goldsrc::ResourceTransitionControlErrorCode::truncated_resource_location;
        expected_parser_offset = semantic.size();
        expected_cursor_offset = 1U;
        expected_opcode = 45U;
        expected_required_opcode = 45U;
    }
    SECTION("overlong location reports a numeric body offset not its raw byte")
    {
        std::array<char, 129U> overlong_location;
        overlong_location.fill('x');
        semantic = with_resource_location(
            second_semantic_payload(),
            std::string_view{overlong_location.data(), overlong_location.size()});
        semantic.insert(semantic.begin(), std::byte{1U});
        expected_error = goldsrc::ResourceTransitionControlErrorCode::resource_location_too_long;
        expected_parser_offset = 1U +
            goldsrc::kResourceTransitionControlMessageSize + 1U + 128U;
        expected_cursor_offset = 1U;
        expected_opcode = 45U;
        expected_required_opcode = 45U;
    }

    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(semantic)));
    stage.update(epoch + 4ms);
    REQUIRE(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->control_code == expected_error);
    CHECK(stage.error()->context.find(location) == std::string_view::npos);
    REQUIRE(stage.error()->failure_metadata);
    const auto& metadata = *stage.error()->failure_metadata;
    CHECK(metadata.parser_error == expected_error);
    CHECK(metadata.parser_error_byte_offset == expected_parser_offset);
    CHECK(metadata.cursor_byte_offset == expected_cursor_offset);
    CHECK(metadata.cursor_bit_offset == 0U);
    CHECK(metadata.expected_opcode == expected_required_opcode);
    CHECK(metadata.actual_opcode == expected_opcode);
    CHECK(metadata.cursor_byte_value == expected_opcode);
    CHECK(metadata.cursor_boundary_kind ==
          goldsrc::ResourceTransitionCursorBoundaryKind::validated_message_boundary);
    CHECK(metadata.wire_encoding == goldsrc::ResourceTransitionWireEncodingKind::bzip2);
    CHECK(metadata.decoded_byte_count == semantic.size());
    CHECK(stage.cleanup_count() == 1U);
    while (const auto event = stage.poll_event()) {
        CHECK(event->type != goldsrc::ResourceTransitionStageEventType::transition_control_decoded);
        CHECK(event->type != goldsrc::ResourceTransitionStageEventType::neutral_opcode43_boundary);
    }
}

TEST_CASE("Resource transfer waiting for a new client packet progresses after non-reliable sendres ACK",
          "[goldsrc][resource-transition][stage][idle-poll][regression]") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'421U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    auto config = test_config();
    auto& driver_config = config.user_info.movement_environment.delta.pre_resource.initial_signon.driver;
    driver_config.channel_inactivity_timeout = 2s;
    const auto poll_enabled = GENERATE(false,true);
    if (poll_enabled) driver_config.idle_poll_interval = 200ms;
    goldsrc::ResourceTransitionStage stage{transport,remote,config};
    const auto driven = drive_to_transition_request(stage,transport,remote,epoch);
    const auto before_ack = transport.sent.size();
    transport.queue(remote,server_packet(2U,false,
        driven.transition_request.header.sequence.sequence.value(),
        false,{})); // second client reliable unit has toggle zero
    stage.update(epoch + 4ms);
    REQUIRE(stage.transition_request_acknowledged());
    REQUIRE(stage.state() == goldsrc::ResourceTransitionStageState::waiting_for_server_transfer);
    while (stage.poll_event()) {}
    REQUIRE(transport.sent.size() == before_ack);
    stage.update(epoch + 204ms);
    CHECK(stage.initial_request_queue_count() == 1U);
    CHECK(stage.transition_request_queue_count() == 1U);
    if (!poll_enabled) {
        // Reproduces the previous silence with the strict historical profile.
        CHECK(transport.sent.size() == before_ack);
        CHECK_FALSE(stage.result());
        CHECK(stage.state() == goldsrc::ResourceTransitionStageState::waiting_for_server_transfer);
    } else {
        REQUIRE(transport.sent.size() == before_ack + 1U);
        const auto poll = decode_sent(transport.sent.back());
        CHECK_FALSE(poll.header.sequence.flags.reliable);
        CHECK_FALSE(poll.header.sequence.flags.fragmented);
        CHECK(std::ranges::all_of(poll.payload,[](const auto value) { return value == std::byte{1U}; }));
        // Fake unspawned server releases queued resource data only after this
        // fresh client sequence, as the server's send-message gate permits.
        transport.queue(remote,server_packet(3U,false,
            poll.header.sequence.sequence.value(),
            poll.header.sequence.flags.reliable,
            service_envelope(second_semantic_payload())));
        stage.update(epoch + 205ms);
        REQUIRE(stage.result());
        CHECK(stage.state() == goldsrc::ResourceTransitionStageState::neutral_opcode43_boundary_reached);
        CHECK(stage.result()->source_payload().source_sequence() == 3U);
        CHECK(stage.transition_request_acknowledged());
    }
}

TEST_CASE("Resource-transition dispatcher consumes a pre-ACK NOP-only payload once",
          "[goldsrc][resource-transition][stage][dispatch][nop][pre-ack][ordering][regression]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'421U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    const auto nops = std::vector<std::byte>(8U, std::byte{1U});
    transport.queue(
        remote,
        normal_fragment_packet(
            2U,
            true,
            driven.transition_request.header.sequence.sequence.value(),
            true,
            1U,
            1U,
            service_envelope(nops)));
    stage.update(epoch + 4ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
    CHECK_FALSE(stage.transition_request_acknowledged());
    const auto retry = decode_sent(transport.sent.back());

    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            retry.header.sequence.sequence.value(),
            false,
            {}));
    stage.update(epoch + 5ms);
    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::waiting_for_server_transfer);
    CHECK(stage.transition_request_acknowledged());
    CHECK_FALSE(stage.result());

    transport.queue(
        remote,
        server_packet(
            4U,
            false,
            retry.header.sequence.sequence.value(),
            false,
            service_envelope(second_semantic_payload())));
    stage.update(epoch + 6ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::
                neutral_opcode43_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->source_payload().source_sequence() == 4U);

    std::size_t nop_events = 0U;
    std::size_t consumed_payload_events = 0U;
    while (const auto event = stage.poll_event()) {
        if (event->type ==
            goldsrc::ResourceTransitionStageEventType::
                intermediate_message_decoded) {
            ++nop_events;
        }
        if (event->type ==
            goldsrc::ResourceTransitionStageEventType::
                intermediate_payload_consumed) {
            ++consumed_payload_events;
            CHECK(event->byte_count == nops.size());
        }
    }
    CHECK(nop_events == nops.size());
    CHECK(consumed_payload_events == 1U);
}

TEST_CASE("User-info and resource-transition stage limits are positive and hard capped",
          "[goldsrc][userinfo][resource-transition][stage][limits][negative]")
{
    auto user_info = goldsrc::UserInfoSignonStageConfig{};
    CHECK(goldsrc::valid_user_info_signon_stage_configuration(user_info));
    user_info.maximum_stage_events = goldsrc::kMaximumUserInfoStageEvents;
    CHECK(goldsrc::valid_user_info_signon_stage_configuration(user_info));
    user_info.maximum_stage_events = 0U;
    CHECK_FALSE(goldsrc::valid_user_info_signon_stage_configuration(user_info));
    user_info.maximum_stage_events = goldsrc::kMaximumUserInfoStageEvents + 1U;
    CHECK_FALSE(goldsrc::valid_user_info_signon_stage_configuration(user_info));

    auto transition = goldsrc::ResourceTransitionStageConfig{};
    CHECK(goldsrc::valid_resource_transition_stage_configuration(transition));

    transition.maximum_second_service_payload_size =
        goldsrc::kMaximumSecondServicePayloadSize;
    CHECK(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_second_service_payload_size = 0U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_second_service_payload_size =
        goldsrc::kMaximumSecondServicePayloadSize + 1U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));

    transition = goldsrc::ResourceTransitionStageConfig{};
    transition.maximum_stage_events =
        goldsrc::kMaximumResourceTransitionStageEvents;
    CHECK(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_stage_events = 0U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_stage_events =
        goldsrc::kMaximumResourceTransitionStageEvents + 1U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));

    transition = goldsrc::ResourceTransitionStageConfig{};
    transition.maximum_driver_events_per_update =
        goldsrc::kMaximumResourceTransitionDriverEventsPerUpdate;
    CHECK(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_driver_events_per_update = 0U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_driver_events_per_update =
        goldsrc::kMaximumResourceTransitionDriverEventsPerUpdate + 1U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));

    transition = goldsrc::ResourceTransitionStageConfig{};
    transition.maximum_pre_ack_payloads = 0U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_pre_ack_payloads = goldsrc::kMaximumTransitionPreAckPayloads;
    CHECK(goldsrc::valid_resource_transition_stage_configuration(transition));
    ++transition.maximum_pre_ack_payloads;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition = goldsrc::ResourceTransitionStageConfig{};
    transition.maximum_pre_ack_payload_bytes = 0U;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));
    transition.maximum_pre_ack_payload_bytes = goldsrc::kMaximumTransitionPreAckBytes;
    CHECK(goldsrc::valid_resource_transition_stage_configuration(transition));
    ++transition.maximum_pre_ack_payload_bytes;
    CHECK_FALSE(goldsrc::valid_resource_transition_stage_configuration(transition));

    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'400U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    auto invalid = test_config();
    invalid.maximum_driver_events_per_update = 0U;
    goldsrc::ResourceTransitionStage stage{transport, remote, invalid};
    CHECK_FALSE(stage.start(epoch, transport.local));
    CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::invalid_configuration);
    CHECK(transport.sent.empty());
}

TEST_CASE("Resource-transition stage reports a bounded reliable queue failure",
          "[goldsrc][resource-transition][stage][queue][negative][lifetime]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'414U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    auto config = test_config();
    auto& driver = config.user_info.movement_environment.delta.pre_resource
                       .initial_signon.driver;
    driver.maximum_unfragmented_reliable_payload =
        goldsrc::kInitialSignonRequestSize;
    driver.maximum_pending_reliable_payload =
        goldsrc::kInitialSignonRequestSize;
    REQUIRE(goldsrc::valid_resource_transition_stage_configuration(config));

    std::size_t releases = 0U;
    goldsrc::ResourceTransitionStage stage{transport, remote, config};
    REQUIRE(stage.start(
        epoch,
        transport.local,
        std::make_unique<CountingLifetime>(releases)));
    stage.update(epoch + 1ms);
    REQUIRE(transport.sent.size() == 1U);
    const auto initial = decode_sent(transport.sent.front());
    const std::array exact_new{
        std::byte{0x03U}, std::byte{'n'}, std::byte{'e'}, std::byte{'w'},
        std::byte{0U}};
    REQUIRE(initial.payload.size() >= exact_new.size());
    CHECK(std::ranges::equal(
        std::span<const std::byte>{initial.payload}.first(exact_new.size()),
        exact_new));

    transport.queue(
        remote,
        server_packet(
            1U,
            true,
            initial.header.sequence.sequence.value(),
            initial.header.sequence.flags.reliable,
            service_envelope(first_semantic_payload())));
    stage.update(epoch + 2ms);

    CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::request_queue_failed);
    CHECK(stage.error()->driver_code ==
          goldsrc::NetchanDriverErrorCode::reliable_queue_failed);
    CHECK(stage.initial_request_queue_count() == 1U);
    CHECK(stage.transition_request_queue_count() == 0U);
    CHECK(count_exact_transition_packets(transport) == 0U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    const auto sends_at_failure = transport.sent.size();
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.sent.size() == sends_at_failure);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Resource-transition stage ignores a wrong endpoint before the valid transfer",
          "[goldsrc][resource-transition][stage][endpoint][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'401U);
    const auto rogue = network::NetworkAddress::loopback(28'402U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    transport.queue(
        rogue,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(second_semantic_payload())));
    stage.update(epoch + 4ms);
    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
    CHECK_FALSE(stage.result());
    deliver_second_transfer(stage, transport, remote, driven, epoch + 5ms);
    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::
              neutral_opcode43_boundary_reached);
}

TEST_CASE("Resource-transition dispatcher rejects unknown message without byte resync",
          "[goldsrc][resource-transition][stage][dispatch][unknown-opcode][negative][transaction][regression]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'403U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    std::optional<goldsrc::ResourceTransitionTraceEvent> failure_trace;
    goldsrc::ResourceTransitionStage stage{
        transport,
        remote,
        test_config(),
        {},
        {},
        {},
        {},
        {},
        [&failure_trace](const auto& event) {
            if (event.classification ==
                goldsrc::ResourceTransitionTraceClassification::
                    unsupported_message) {
                failure_trace = event;
            }
        }};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    auto malformed = second_semantic_payload();
    malformed[0U] = std::byte{44U};
    malformed[4U] = std::byte{45U};
    const auto wire_payload = service_envelope(malformed);
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            wire_payload));
    stage.update(epoch + 4ms);
    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::unsupported_message);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::
              intermediate_message_decode_failed);
    CHECK(stage.error()->intermediate_control_code ==
          goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
    CHECK_FALSE(stage.error()->control_code);
    REQUIRE(stage.error()->failure_metadata);
    const auto& metadata = *stage.error()->failure_metadata;
    CHECK(metadata.expected_opcode == 45U);
    CHECK(metadata.actual_opcode == 44U);
    CHECK(metadata.cursor_byte_value == 44U);
    CHECK(metadata.cursor_byte_offset == 0U);
    CHECK(metadata.cursor_bit_offset == 0U);
    CHECK(metadata.cursor_boundary_kind ==
          goldsrc::ResourceTransitionCursorBoundaryKind::
              validated_message_boundary);
    CHECK(metadata.payload_ordinal == 1U);
    CHECK(metadata.payload_ordinal_scope ==
          goldsrc::ResourceTransitionPayloadOrdinalScope::
              resource_transition_nonempty_service_payload);
    CHECK(metadata.direction ==
          goldsrc::NetchanDirection::server_to_client);
    CHECK(metadata.source_sequence == 2U);
    CHECK(metadata.source_acknowledgement ==
          driven.transition_request.header.sequence.sequence.value());
    CHECK(metadata.source_reliable == false);
    CHECK(metadata.reassembled == false);
    CHECK(metadata.wire_encoding ==
          goldsrc::ResourceTransitionWireEncodingKind::bzip2);
    CHECK(metadata.wire_byte_count == wire_payload.size());
    CHECK(metadata.decoded_byte_count == malformed.size());
    CHECK(metadata.pending_suffix_byte_offset == 0U);
    CHECK(metadata.pending_suffix_bit_offset == 0U);
    CHECK(metadata.request_queued);
    CHECK(metadata.request_transmitted);
    CHECK(metadata.request_acknowledged);
    CHECK(metadata.request_transmit_sequence ==
          driven.transition_request.header.sequence.sequence.value());
    CHECK(metadata.request_acknowledgement_sequence ==
          driven.transition_request.header.sequence.sequence.value());
    CHECK(metadata.last_successful_message_category ==
          goldsrc::ResourceTransitionLastMessageCategory::
              user_info_first_batch);
    CHECK(metadata.last_successful_message_byte_offset);
    CHECK(metadata.last_successful_message_end_byte_offset);
    CHECK(metadata.intermediate_parser_error ==
          goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
    CHECK_FALSE(metadata.parser_error);
    REQUIRE(failure_trace);
    REQUIRE(failure_trace->failure_metadata);
    CHECK(failure_trace->failure_metadata->cursor_byte_value == 44U);
    CHECK(failure_trace->failure_metadata->actual_opcode == 44U);
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Resource-transition dispatcher rejects a truncated known intermediate message",
          "[goldsrc][resource-transition][stage][dispatch][truncated][negative][regression]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'422U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(std::vector<std::byte>{
                std::byte{7U}, std::byte{0U}, std::byte{0U}})));
    stage.update(epoch + 4ms);

    CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::
              intermediate_message_decode_failed);
    CHECK(stage.error()->intermediate_control_code ==
          goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
    REQUIRE(stage.error()->failure_metadata);
    CHECK(stage.error()->failure_metadata->actual_opcode == 7U);
    CHECK(stage.error()->failure_metadata->cursor_byte_offset == 0U);
    CHECK(stage.error()->failure_metadata->intermediate_parser_error ==
          goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
    while (const auto event = stage.poll_event()) {
        CHECK(event->type !=
              goldsrc::ResourceTransitionStageEventType::
                  transition_control_decoded);
    }
}

TEST_CASE("Resource-transition stage rejects malformed opcode-45 framing transactionally",
          "[goldsrc][resource-transition][stage][control][negative][transaction]")
{
    SECTION("truncated eight-byte body")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'415U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        auto malformed = second_semantic_payload();
        malformed.resize(goldsrc::kResourceTransitionControlMessageSize - 1U);
        transport.queue(
            remote,
            server_packet(
                2U,
                false,
                driven.transition_request.header.sequence.sequence.value(),
                false,
                service_envelope(malformed)));
        stage.update(epoch + 4ms);

        CHECK(stage.state() ==
              goldsrc::ResourceTransitionStageState::protocol_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceTransitionStageErrorCode::
                  transition_control_decode_failed);
        CHECK(stage.error()->control_code ==
              goldsrc::ResourceTransitionControlErrorCode::truncated_body);
        CHECK(stage.cleanup_count() == 1U);
        while (const auto event = stage.poll_event()) {
            CHECK(event->type !=
                  goldsrc::ResourceTransitionStageEventType::
                      second_service_transfer_received);
            CHECK(event->type !=
                  goldsrc::ResourceTransitionStageEventType::
                      transition_control_decoded);
            CHECK(event->type !=
                  goldsrc::ResourceTransitionStageEventType::
                      neutral_opcode43_boundary);
        }
    }

    SECTION("unexpected byte inserted before opcode 43")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'416U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        auto malformed = second_semantic_payload();
        malformed.insert(
            malformed.begin() + static_cast<std::ptrdiff_t>(
                                    goldsrc::kResourceTransitionControlMessageSize),
            std::byte{0x7fU});
        transport.queue(
            remote,
            server_packet(
                2U,
                false,
                driven.transition_request.header.sequence.sequence.value(),
                false,
                service_envelope(malformed)));
        stage.update(epoch + 4ms);

        CHECK(stage.state() ==
              goldsrc::ResourceTransitionStageState::protocol_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceTransitionStageErrorCode::
                  transition_control_decode_failed);
        CHECK(stage.error()->control_code ==
              goldsrc::ResourceTransitionControlErrorCode::
                  wrong_next_boundary_opcode);
        CHECK(stage.cleanup_count() == 1U);
        while (const auto event = stage.poll_event()) {
            CHECK(event->type !=
                  goldsrc::ResourceTransitionStageEventType::
                      second_service_transfer_received);
            CHECK(event->type !=
                  goldsrc::ResourceTransitionStageEventType::
                      transition_control_decoded);
            CHECK(event->type !=
                  goldsrc::ResourceTransitionStageEventType::
                      neutral_opcode43_boundary);
        }
    }
}

TEST_CASE("Resource-transition stage buffers one transfer until the matching ACK",
          "[goldsrc][resource-transition][stage][drop-ack][pre-ack][reliable]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'404U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    // The numeric acknowledgement covers sendres, but the old reliable
    // generation does not complete it. The owning payload must remain buffered.
    const auto envelope = service_envelope(second_semantic_payload());
    transport.queue(
        remote,
        normal_fragment_packet(
            2U,
            true,
            driven.transition_request.header.sequence.sequence.value(),
            true,
            1U,
            1U,
            envelope));
    stage.update(epoch + 4ms);
    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
    CHECK_FALSE(stage.result());
    CHECK_FALSE(stage.transition_request_acknowledged());
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(count_exact_transition_packets(transport) == 1U);

    const auto transport_ack = decode_sent(transport.sent.back());
    transport.queue(
        remote,
        normal_fragment_packet(
            3U,
            true,
            transport_ack.header.sequence.sequence.value(),
            true,
            1U,
            2U,
            std::vector<std::byte>(
                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                std::byte{0x31U})));
    stage.update(epoch + 5ms);
    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(count_exact_transition_packets(transport) == 2U);
    const auto retry = decode_sent(transport.sent.back());

    // A later matching acknowledgement commits the buffered transfer. Its
    // contemporaneous empty payload is never interpreted after the boundary.
    transport.queue(
        remote,
        server_packet(
            4U,
            false,
            retry.header.sequence.sequence.value(),
            false,
            {}));
    stage.update(epoch + 6ms);
    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::
              neutral_opcode43_boundary_reached);
    CHECK(stage.transition_request_acknowledged());
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(count_exact_transition_packets(transport) == 2U);
}

TEST_CASE("Resource-transition stage enforces an explicitly single pre-ACK payload bound",
          "[goldsrc][resource-transition][stage][pre-ack][overflow][negative]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'413U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_pre_ack_payloads = 1U;
    goldsrc::ResourceTransitionStage stage{transport, remote, config};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    const auto envelope = service_envelope(second_semantic_payload());
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            true,
            envelope));
    stage.update(epoch + 4ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
    CHECK_FALSE(stage.transition_request_acknowledged());
    const auto retry = decode_sent(transport.sent.back());

    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            retry.header.sequence.sequence.value(),
            true,
            envelope));
    stage.update(epoch + 5ms);
    CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::
              service_payload_before_ack_overflow);
    CHECK(stage.error()->context.find("count=1") != std::string::npos);
    CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Resource-transition pre-ACK queue preserves query ordering and source provenance",
          "[goldsrc][resource-transition][stage][pre-ack][queue][public-server][no-stock]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'460U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
    const std::vector<std::vector<std::byte>> payloads{
        {std::byte{57U}, std::byte{'a'}, std::byte{0U}},
        {std::byte{58U}, std::byte{3U}, std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{'b'}, std::byte{0U}},
        second_semantic_payload()};
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        transport.queue(remote, server_packet(static_cast<std::uint32_t>(index + 2U), index == 0U,
            driven.transition_request.header.sequence.sequence.value(), true,
            service_envelope(payloads[index])));
        stage.update(epoch + std::chrono::milliseconds{static_cast<int>(index + 4U)});
        REQUIRE(stage.state() == goldsrc::ResourceTransitionStageState::waiting_for_request_ack);
        CHECK_FALSE(stage.result());
        CHECK_FALSE(stage.transition_request_acknowledged());
    }
    std::vector<unsigned> observed;
    while (const auto event = stage.poll_event())
        CHECK(event->type != goldsrc::ResourceTransitionStageEventType::intermediate_message_decoded);
    // The first reliable carrier caused a real later client transport ACK.
    // Changing only the reliable bit of an already seen numeric ACK is not
    // a new acknowledgement and must not release the queued transfer.
    const auto covering_ack = decode_sent(transport.sent.back()).header.sequence.sequence.value();
    REQUIRE(covering_ack > driven.transition_request.header.sequence.sequence.value());
    transport.queue(remote, server_packet(5U, false, covering_ack, false, {}));
    stage.update(epoch + 7ms);
    REQUIRE(stage.transition_request_acknowledged());
    stage.update(epoch + 8ms);
    stage.update(epoch + 9ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->source_payload().source_sequence() == 4U);
    CHECK(stage.result()->source_payload().received_at() == epoch + 6ms);
    while (const auto event = stage.poll_event())
        if (event->type == goldsrc::ResourceTransitionStageEventType::intermediate_message_decoded) {
            REQUIRE(event->opcode);
            observed.push_back(*event->opcode);
        }
    CHECK(observed == std::vector<unsigned>{57U, 58U});
    CHECK(stage.transition_request_queue_count() == 1U);
}

TEST_CASE("Resource-transition pre-ACK queue enforces aggregate bytes without early decode",
          "[goldsrc][resource-transition][stage][pre-ack][queue][limits][no-stock]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'461U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    const auto payload = service_envelope(std::vector<std::byte>{std::byte{1U}});
    auto config = test_config();
    config.maximum_pre_ack_payload_bytes = payload.size();
    goldsrc::ResourceTransitionStage stage{transport, remote, config};
    const auto driven = drive_to_transition_request(stage, transport, remote, epoch);
    for (std::uint32_t sequence_value = 2U; sequence_value <= 3U; ++sequence_value) {
        transport.queue(remote, server_packet(sequence_value, false,
            driven.transition_request.header.sequence.sequence.value(), true, payload));
        stage.update(epoch + std::chrono::milliseconds{sequence_value + 2U});
    }
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceTransitionStageErrorCode::service_payload_before_ack_overflow);
    CHECK(stage.error()->context.find("count=1") != std::string::npos);
    CHECK(stage.error()->context.find("bytes=" + std::to_string(payload.size())) != std::string::npos);
    CHECK_FALSE(stage.transition_request_acknowledged());
    CHECK_FALSE(stage.result());
    while (const auto event = stage.poll_event())
        CHECK(event->type != goldsrc::ResourceTransitionStageEventType::intermediate_message_decoded);
}

TEST_CASE("Resource-transition stage rejects malformed second-transfer envelopes",
          "[goldsrc][resource-transition][stage][bzip2][negative][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'405U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            std::vector<std::byte>{
                std::byte{0x42U}, std::byte{0x5aU}, std::byte{0x32U},
                std::byte{0U}, std::byte{0x42U}, std::byte{0x5aU}}));
    stage.update(epoch + 4ms);
    CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::
              second_payload_envelope_decode_failed);
}

TEST_CASE("Resource-transition stage enforces exact opcode-43 cursor",
          "[goldsrc][resource-transition][stage][cursor][negative][security]")
{
    SECTION("wrong following opcode")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'406U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        auto semantic = second_semantic_payload();
        semantic[9U] = std::byte{42U};
        transport.queue(
            remote,
            server_packet(
                2U,
                false,
                driven.transition_request.header.sequence.sequence.value(),
                false,
                service_envelope(semantic)));
        stage.update(epoch + 4ms);
        CHECK(stage.state() ==
              goldsrc::ResourceTransitionStageState::protocol_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->control_code ==
              goldsrc::ResourceTransitionControlErrorCode::
                  wrong_next_boundary_opcode);
        REQUIRE(stage.error()->failure_metadata);
        CHECK(stage.error()->failure_metadata->expected_opcode == 43U);
        CHECK(stage.error()->failure_metadata->actual_opcode == 42U);
        CHECK(stage.error()->failure_metadata->cursor_byte_offset == 9U);
        CHECK(stage.error()->failure_metadata->parser_error_byte_offset == 9U);
    }

    SECTION("truncated boundary opcode")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'407U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        auto semantic = second_semantic_payload();
        semantic.resize(9U);
        transport.queue(
            remote,
            server_packet(
                2U,
                false,
                driven.transition_request.header.sequence.sequence.value(),
                false,
                service_envelope(semantic)));
        stage.update(epoch + 4ms);
        CHECK(stage.state() ==
              goldsrc::ResourceTransitionStageState::protocol_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->control_code ==
              goldsrc::ResourceTransitionControlErrorCode::
                  missing_next_boundary);
        REQUIRE(stage.error()->failure_metadata);
        CHECK(stage.error()->failure_metadata->expected_opcode == 43U);
        CHECK_FALSE(stage.error()->failure_metadata->actual_opcode);
        CHECK_FALSE(stage.error()->failure_metadata->cursor_byte_value);
        CHECK(stage.error()->failure_metadata->cursor_byte_offset == 9U);
        CHECK(stage.error()->failure_metadata->parser_error_byte_offset == 9U);
    }
}

TEST_CASE("Resource-transition stage timeout cancellation and backpressure are terminal",
          "[goldsrc][resource-transition][stage][timeout][cancel][backpressure]")
{
    SECTION("request acknowledgement timeout")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'408U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        static_cast<void>(drive_to_transition_request(
            stage, transport, remote, epoch));
        stage.update(epoch + 103ms);
        CHECK(stage.state() == goldsrc::ResourceTransitionStageState::timed_out);
        CHECK(stage.cleanup_count() == 1U);
    }

    SECTION("cooperative cancellation releases once")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'409U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        std::size_t releases = 0U;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        static_cast<void>(drive_to_transition_request(
            stage,
            transport,
            remote,
            epoch,
            false,
            std::make_unique<CountingLifetime>(releases)));
        stage.cancel(epoch + 4ms);
        CHECK(stage.state() == goldsrc::ResourceTransitionStageState::cancelled);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
        stage.cancel(epoch + 5ms);
        stage.update(epoch + 6ms);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("bounded public event queue rejects acknowledgement atomically")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'410U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        auto config = test_config();
        config.maximum_stage_events = 2U;
        goldsrc::ResourceTransitionStage stage{transport, remote, config};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_second_transfer(stage, transport, remote, driven, epoch + 4ms);
        CHECK(stage.state() ==
              goldsrc::ResourceTransitionStageState::backpressure);
        CHECK_FALSE(stage.result());
        CHECK(stage.cleanup_count() == 1U);
    }
}

TEST_CASE("Resource-transition stage propagates secondary stream and driver failures",
          "[goldsrc][resource-transition][stage][driver][secondary][security]")
{
    SECTION("secondary fragment stream remains typed pending")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'411U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        transport.queue(
            remote,
            secondary_fragment_packet(
                2U,
                driven.transition_request.header.sequence.sequence.value(),
                false));
        stage.update(epoch + 4ms);
        CHECK(stage.state() ==
              goldsrc::ResourceTransitionStageState::secondary_stream_pending);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::secondary_stream_pending_m3);
    }

    SECTION("receive error is a network terminal")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(28'412U);
        const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
        goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
        static_cast<void>(drive_to_transition_request(
            stage, transport, remote, epoch));
        transport.incoming.push_back({
            network::DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic receive failure",
        });
        stage.update(epoch + 4ms);
        CHECK(stage.state() ==
              goldsrc::ResourceTransitionStageState::network_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::receive_failed);
    }
}

TEST_CASE("Resource-transition user-info baseline repeats 20 of 20",
          "[goldsrc][resource-transition][stage][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_baseline(run);
    }
}

TEST_CASE("Dropped transition request retransmission repeats 20 of 20",
          "[goldsrc][resource-transition][stage][drop-request][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_dropped_request(run);
    }
}

TEST_CASE("Fragmented second service transfer repeats 20 of 20",
          "[goldsrc][resource-transition][stage][fragment][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_fragmented_second_transfer(run);
    }
}

TEST_CASE("Multi-userinfo differential repeats 20 of 20",
          "[goldsrc][resource-transition][stage][multi-userinfo][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_baseline(run, true);
    }
}

TEST_CASE("Resource-transition consumes an exactly framed director message without scanning its body",
          "[goldsrc][resource-transition][stage][director-signon][dispatch][security][regression]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'470U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    // svc_director owns one uint8-sized opaque body. Every body byte is also
    // meaningful at a service-message boundary and therefore catches resync.
    const std::vector<std::byte> director{
        std::byte{51U}, std::byte{4U}, std::byte{11U},
        std::byte{43U}, std::byte{45U}, std::byte{58U}};
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(director)));
    stage.update(epoch + 4ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::waiting_for_server_transfer);
    CHECK_FALSE(stage.error());
    CHECK_FALSE(stage.result());
    CHECK(stage.transition_request_queue_count() == 1U);
    std::vector<unsigned> observed_opcodes;
    std::size_t consumed_payload_count = 0U;
    while (const auto event = stage.poll_event()) {
        if (event->type ==
            goldsrc::ResourceTransitionStageEventType::
                intermediate_message_decoded) {
            REQUIRE(event->opcode);
            observed_opcodes.push_back(*event->opcode);
            CHECK(event->byte_offset == 0U);
            CHECK(event->byte_count == director.size());
        }
        if (event->type ==
            goldsrc::ResourceTransitionStageEventType::
                intermediate_payload_consumed) {
            ++consumed_payload_count;
            CHECK(event->byte_count == director.size());
        }
    }
    CHECK(observed_opcodes == std::vector<unsigned>{51U});
    CHECK(consumed_payload_count == 1U);

    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(second_semantic_payload())));
    stage.update(epoch + 5ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceTransitionStageState::
                neutral_opcode43_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->source_payload().source_sequence() == 3U);
    CHECK(stage.result()->control().source_message_offset() == 0U);
    CHECK(stage.result()->boundary().byte_offset() == 9U);
    CHECK(stage.result()->boundary().opcode() == 43U);
    CHECK(stage.transition_request_queue_count() == 1U);
    for (const auto& sent : transport.sent) {
        const auto packet = decode_sent(sent);
        if (packet.payload.empty()) {
            continue;
        }
        const auto opcode = std::to_integer<unsigned>(packet.payload.front());
        CHECK(opcode != 10U);
        CHECK(opcode != 11U);
    }
}

TEST_CASE("Resource-transition rejects malformed or unknown director-adjacent framing transactionally",
          "[goldsrc][resource-transition][stage][director-signon][negative][security][regression]")
{
    const auto malformed = GENERATE(
        (std::vector<std::byte>{std::byte{51U}}),
        (std::vector<std::byte>{
            std::byte{51U}, std::byte{4U}, std::byte{11U},
            std::byte{43U}, std::byte{45U}}));
    CAPTURE(malformed.size());
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'471U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(malformed)));
    stage.update(epoch + 4ms);

    CHECK(stage.state() == goldsrc::ResourceTransitionStageState::protocol_error);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::
              intermediate_message_decode_failed);
    CHECK(stage.error()->intermediate_control_code ==
          goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
    REQUIRE(stage.error()->failure_metadata);
    CHECK(stage.error()->failure_metadata->actual_opcode == 51U);
    CHECK(stage.error()->failure_metadata->cursor_byte_offset == 0U);
    CHECK(stage.error()->failure_metadata->intermediate_message_count == 0U);
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(count_exact_transition_packets(transport) == 1U);
    while (const auto event = stage.poll_event()) {
        CHECK(event->type !=
              goldsrc::ResourceTransitionStageEventType::
                  intermediate_message_decoded);
        CHECK(event->type !=
              goldsrc::ResourceTransitionStageEventType::
                  transition_control_decoded);
        CHECK(event->type !=
              goldsrc::ResourceTransitionStageEventType::
                  neutral_opcode43_boundary);
    }
}

TEST_CASE("Resource-transition does not broaden the director allowlist to an adjacent unknown opcode",
          "[goldsrc][resource-transition][stage][director-signon][unknown-opcode][security][regression]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'472U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    const std::vector<std::byte> unknown{
        std::byte{50U}, std::byte{4U}, std::byte{11U},
        std::byte{43U}, std::byte{45U}, std::byte{58U}};

    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(unknown)));
    stage.update(epoch + 4ms);

    CHECK(stage.state() ==
          goldsrc::ResourceTransitionStageState::unsupported_message);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::
              intermediate_message_decode_failed);
    CHECK(stage.error()->intermediate_control_code ==
          goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
    REQUIRE(stage.error()->failure_metadata);
    CHECK(stage.error()->failure_metadata->actual_opcode == 50U);
    CHECK(stage.error()->failure_metadata->cursor_byte_offset == 0U);
    CHECK(stage.error()->failure_metadata->intermediate_message_count == 0U);
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(count_exact_transition_packets(transport) == 1U);
    while (const auto event = stage.poll_event()) {
        CHECK(event->type !=
              goldsrc::ResourceTransitionStageEventType::
                  intermediate_message_decoded);
        CHECK(event->type !=
              goldsrc::ResourceTransitionStageEventType::
                  transition_control_decoded);
        CHECK(event->type !=
              goldsrc::ResourceTransitionStageEventType::
                  neutral_opcode43_boundary);
    }
}

} // namespace
