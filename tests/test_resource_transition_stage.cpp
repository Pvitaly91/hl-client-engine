#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/resource_transition_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>

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

TEST_CASE("Resource-transition stage rejects an unknown opcode before the boundary",
          "[goldsrc][resource-transition][stage][unknown-opcode][negative][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'403U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    auto malformed = second_semantic_payload();
    malformed[0U] = std::byte{44U};
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
          goldsrc::ResourceTransitionStageState::unsupported_message);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::
              transition_control_decode_failed);
    CHECK(stage.error()->control_code ==
          goldsrc::ResourceTransitionControlErrorCode::wrong_opcode);
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

TEST_CASE("Resource-transition stage rejects a second complete pre-ACK payload",
          "[goldsrc][resource-transition][stage][pre-ack][overflow][negative]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(28'413U);
    const auto epoch = goldsrc::ResourceTransitionStageTimePoint{} + 1s;
    goldsrc::ResourceTransitionStage stage{transport, remote, test_config()};
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
    CHECK(stage.cleanup_count() == 1U);
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

} // namespace
