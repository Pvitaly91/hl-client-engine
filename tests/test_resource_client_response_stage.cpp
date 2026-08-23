#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/resource_client_response_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/resource_consistency/provider.hpp>

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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace response_fixture =
    hlclient::test::resource_client_response_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

// Independently authored stage fixture. It deliberately names the stage's
// evidence-gated tempdecal profile rather than asking the production builder
// to produce the expected bytes.
inline constexpr std::array<std::byte, 41U> kExactTempdecalResponse{
    std::byte{0x05U},
    std::byte{0x01U}, std::byte{0x00U},
    std::byte{'t'}, std::byte{'e'}, std::byte{'m'}, std::byte{'p'},
    std::byte{'d'}, std::byte{'e'}, std::byte{'c'}, std::byte{'a'},
    std::byte{'l'}, std::byte{'.'}, std::byte{'w'}, std::byte{'a'},
    std::byte{'d'}, std::byte{0x00U},
    std::byte{0x03U},
    std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x04U}, std::byte{0x03U}, std::byte{0x02U}, std::byte{0x01U},
    std::byte{0x04U},
    std::byte{0xa0U}, std::byte{0xa1U}, std::byte{0xa2U}, std::byte{0xa3U},
    std::byte{0xa4U}, std::byte{0xa5U}, std::byte{0xa6U}, std::byte{0xa7U},
    std::byte{0xa8U}, std::byte{0xa9U}, std::byte{0xaaU}, std::byte{0xabU},
    std::byte{0xacU}, std::byte{0xadU}, std::byte{0xaeU}, std::byte{0xafU},
};

struct SentDatagram {
    network::NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeTransport final : public network::IDatagramTransport {
public:
    [[nodiscard]] network::DatagramLocalAddressResult local_address()
        const override
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

    network::NetworkAddress local{network::NetworkAddress::loopback(31'703U)};
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
};

class CountingConnectionLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingConnectionLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingConnectionLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

class CountingConsistencyLifetime final
    : public consistency::IResourceConsistencySessionLifetime {
public:
    explicit CountingConsistencyLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingConsistencyLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

class FakeConsistencyOperation final
    : public consistency::ResourceConsistencyOperation {
public:
    FakeConsistencyOperation(
        const bool pending_forever,
        std::size_t& update_count,
        std::size_t& cancel_count,
        std::size_t& lifetime_releases) noexcept
        : pending_forever_{pending_forever},
          update_count_{update_count},
          cancel_count_{cancel_count},
          lifetime_releases_{lifetime_releases}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyUpdateResult update()
        override
    {
        ++update_count_;
        if (pending_forever_) {
            return consistency::ResourceConsistencyUpdateResult::pending();
        }

        auto created = consistency::make_resource_consistency_material(
            0x01020304U,
            response_fixture::kSyntheticOpaqueMaterial);
        REQUIRE(created);
        return consistency::ResourceConsistencyUpdateResult::succeeded(
            consistency::ResourceConsistencySession{
                std::move(*created.material),
                std::make_unique<CountingConsistencyLifetime>(
                    lifetime_releases_),
            });
    }

    void cancel() noexcept override
    {
        if (!cancelled_) {
            cancelled_ = true;
            ++cancel_count_;
        }
    }

private:
    bool pending_forever_{false};
    std::size_t& update_count_;
    std::size_t& cancel_count_;
    std::size_t& lifetime_releases_;
    bool cancelled_{false};
};

class FakeConsistencyProvider final
    : public consistency::IResourceConsistencyProvider {
public:
    explicit FakeConsistencyProvider(const bool pending_forever = false)
        noexcept
        : pending_forever_{pending_forever}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyBeginResult begin(
        const consistency::ResourceConsistencyRequirements& requirements)
        override
    {
        ++begin_count;
        observed_material_count = requirements.material_count();
        observed_opaque_byte_count = requirements.opaque_bytes_per_material();
        return consistency::ResourceConsistencyBeginResult::started(
            std::make_unique<FakeConsistencyOperation>(
                pending_forever_,
                update_count,
                cancel_count,
                lifetime_releases));
    }

    bool pending_forever_{false};
    std::size_t begin_count{0U};
    std::size_t update_count{0U};
    std::size_t cancel_count{0U};
    std::size_t lifetime_releases{0U};
    std::size_t observed_material_count{0U};
    std::size_t observed_opaque_byte_count{0U};
};

class SensitiveFailureProvider final
    : public consistency::IResourceConsistencyProvider {
public:
    [[nodiscard]] consistency::ResourceConsistencyBeginResult begin(
        const consistency::ResourceConsistencyRequirements&) override
    {
        ++begin_count;
        return consistency::ResourceConsistencyBeginResult::failed({
            consistency::ResourceConsistencyErrorCode::provider_error,
            R"(C:\private\tempdecal.wad material=a0a1)",
        });
    }

    std::size_t begin_count{0U};
};

class InvalidUpdateOperation final
    : public consistency::ResourceConsistencyOperation {
public:
    explicit InvalidUpdateOperation(
        std::size_t& update_count,
        std::size_t& cancel_count) noexcept
        : update_count_{update_count}, cancel_count_{cancel_count}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyUpdateResult update()
        override
    {
        ++update_count_;
        return {
            static_cast<consistency::ResourceConsistencyUpdateState>(0xffU),
            std::nullopt,
            std::nullopt,
        };
    }

    void cancel() noexcept override
    {
        if (!cancelled_) {
            cancelled_ = true;
            ++cancel_count_;
        }
    }

private:
    std::size_t& update_count_;
    std::size_t& cancel_count_;
    bool cancelled_{false};
};

class InvalidUpdateProvider final
    : public consistency::IResourceConsistencyProvider {
public:
    [[nodiscard]] consistency::ResourceConsistencyBeginResult begin(
        const consistency::ResourceConsistencyRequirements&) override
    {
        ++begin_count;
        return consistency::ResourceConsistencyBeginResult::started(
            std::make_unique<InvalidUpdateOperation>(
                update_count,
                cancel_count));
    }

    std::size_t begin_count{0U};
    std::size_t update_count{0U};
    std::size_t cancel_count{0U};
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
        std::byte{0x42U}, std::byte{0x5aU},
        std::byte{0x32U}, std::byte{0x00U},
    };
    std::ranges::transform(
        compressed,
        std::back_inserter(envelope),
        [](const char value) {
            return static_cast<std::byte>(
                static_cast<unsigned char>(value));
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t packet_sequence,
    const bool reliable,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    std::vector<std::byte> payload = {})
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

[[nodiscard]] goldsrc::ResourceClientResponseStageConfig test_config()
{
    goldsrc::ResourceClientResponseStageConfig config;
    auto& transition = config.resource_list.transition;
    auto& driver = transition.user_info.movement_environment.delta.pre_resource
                       .initial_signon.driver;
    driver.channel_inactivity_timeout = 100ms;
    driver.fragment_transfer_timeout = 100ms;
    driver.maximum_datagrams_per_update = 16U;
    driver.maximum_outgoing_packets_per_update = 8U;
    driver.maximum_events = 64U;
    transition.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_events = 64U;
    transition.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_driver_events_per_update = 64U;
    transition.user_info.movement_environment.delta.pre_resource.maximum_events =
        64U;
    transition.user_info.movement_environment.delta.maximum_events = 64U;
    transition.user_info.movement_environment.maximum_events = 64U;
    transition.user_info.maximum_stage_events = 64U;
    transition.maximum_stage_events = 64U;
    transition.maximum_driver_events_per_update = 64U;
    config.resource_list.maximum_stage_events = 64U;
    config.maximum_driver_events_per_update = 64U;
    config.response.maximum_response_stage_events = 64U;
    return config;
}

[[nodiscard]] std::vector<std::vector<std::byte>> schemas()
{
    return {
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("bravo_t", delta_fixture::kSchemaBravoFields),
    };
}

[[nodiscard]] std::vector<std::byte> first_semantic_payload()
{
    std::vector<std::byte> post_delta;
    move_fixture::append_move_vars_body(post_delta);
    move_fixture::append_confirmed_controls(post_delta);
    post_delta.insert(
        post_delta.end(),
        user_fixture::kExactUserInfoMessage.begin(),
        user_fixture::kExactUserInfoMessage.end());
    return delta_fixture::service_payload(
        schemas(),
        goldsrc::kMoveVarsOpcode,
        post_delta);
}

[[nodiscard]] std::vector<std::byte> resource_semantic_payload()
{
    constexpr std::array prefix{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
    };
    std::vector<std::byte> payload{prefix.begin(), prefix.end()};
    payload.insert(
        payload.end(),
        resource_list_test_fixture::kExactResourceListMessage.begin(),
        resource_list_test_fixture::kExactResourceListMessage.end());
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
    goldsrc::ClientToServerNetchanPacket request;
};

template <typename Stage>
[[nodiscard]] DrivenTransition drive_to_transition_request(
    Stage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::ResourceClientResponseStageTimePoint epoch,
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
            true,
            service_envelope(first_semantic_payload())));
    stage.update(epoch + 2ms);
    CHECK(stage.initial_request_queue_count() == 1U);
    CHECK(stage.transition_request_queue_count() == 1U);

    stage.update(epoch + 3ms);
    REQUIRE(transport.sent.size() >= 3U);
    const auto transition = decode_sent(transport.sent.back());
    constexpr std::array exact_request{
        std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
        std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
        std::byte{0U},
    };
    REQUIRE(std::ranges::equal(transition.payload, exact_request));
    return DrivenTransition{transition};
}

template <typename Stage>
void deliver_resource_payload(
    Stage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const DrivenTransition& driven,
    const goldsrc::ResourceClientResponseStageTimePoint now)
{
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.request.header.sequence.sequence.value(),
            false,
            service_envelope(resource_semantic_payload())));
    stage.update(now);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket require_response_packet(
    const FakeTransport& transport)
{
    REQUIRE_FALSE(transport.sent.empty());
    const auto response = decode_sent(transport.sent.back());
    REQUIRE(response.header.sequence.flags.reliable);
    REQUIRE(response.header.sequence.flags.fragmented);
    REQUIRE(response.fragments[0U]);
    REQUIRE_FALSE(response.fragments[1U]);
    REQUIRE(response.fragments[0U]->packed_id());
    CHECK(response.fragments[0U]->packed_id()->fragment_index() == 1U);
    CHECK(response.fragments[0U]->packed_id()->fragment_count() == 1U);
    CHECK(response.fragments[0U]->length == kExactTempdecalResponse.size());
    CHECK(response.fragment_payload_size == kExactTempdecalResponse.size());
    CHECK(std::ranges::equal(response.payload, kExactTempdecalResponse));
    return response;
}

[[nodiscard]] std::vector<goldsrc::ResourceClientResponseStageEvent>
drain_events(goldsrc::ResourceClientResponseStage& stage)
{
    std::vector<goldsrc::ResourceClientResponseStageEvent> events;
    while (auto event = stage.poll_event()) {
        events.push_back(std::move(*event));
    }
    return events;
}

[[nodiscard]] std::size_t event_count(
    const std::span<const goldsrc::ResourceClientResponseStageEvent> events,
    const goldsrc::ResourceClientResponseStageEventType type)
{
    return static_cast<std::size_t>(std::ranges::count_if(
        events,
        [type](const auto& event) { return event.type == type; }));
}

[[nodiscard]] constexpr std::array<std::byte, 7U>
next_server_semantic_payload()
{
    return {
        std::byte{0x03U},
        std::byte{'s'}, std::byte{'p'}, std::byte{'a'},
        std::byte{'w'}, std::byte{'n'}, std::byte{0U},
    };
}

void deliver_covering_continuation(
    goldsrc::ResourceClientResponseStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const std::uint32_t incoming_sequence,
    const goldsrc::ClientToServerNetchanPacket& response,
    const goldsrc::ResourceClientResponseStageTimePoint now)
{
    transport.queue(
        remote,
        server_packet(
            incoming_sequence,
            false,
            response.header.sequence.sequence.value(),
            true,
            service_envelope(next_server_semantic_payload())));
    stage.update(now);
}

TEST_CASE("Resource response requires an explicit provider and sends no guessed bytes",
          "[goldsrc][resource-response][stage][provider][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'820U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    std::size_t connection_releases = 0U;
    goldsrc::ResourceClientResponseStage stage{
        transport,
        remote,
        test_config(),
        nullptr};
    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    const auto sends_before_boundary = transport.sent.size();

    deliver_resource_payload(
        stage, transport, remote, driven, epoch + 4ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::
                consistency_provider_required);
    REQUIRE(stage.terminal());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceClientResponseStageErrorCode::provider_required);
    REQUIRE(stage.error()->consistency_code);
    CHECK(*stage.error()->consistency_code ==
          consistency::ResourceConsistencyErrorCode::unavailable);
    CHECK_FALSE(stage.result());
    CHECK(stage.requirements_derivation_count() == 1U);
    CHECK(stage.provider_begin_count() == 0U);
    CHECK(stage.response_build_count() == 0U);
    CHECK(stage.response_queue_count() == 0U);
    CHECK_FALSE(stage.response_transmitted());
    CHECK(transport.sent.size() == sends_before_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(connection_releases == 1U);

    const auto events = drain_events(stage);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  resource_response_requirements_ready) == 1U);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  consistency_provider_required) == 1U);
}

TEST_CASE("Resource response builds and queues the independent semantic fixture once",
          "[goldsrc][resource-response][stage][provider][semantic-once]")
{
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'821U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    goldsrc::ResourceClientResponseStage stage{
        transport,
        remote,
        test_config(),
        &provider};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);

    deliver_resource_payload(
        stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);

    CHECK(stage.state() ==
          goldsrc::ResourceClientResponseStageState::waiting_for_response_ack);
    CHECK(stage.requirements_derivation_count() == 1U);
    CHECK(stage.provider_begin_count() == 1U);
    CHECK(stage.response_build_count() == 1U);
    CHECK(stage.response_queue_count() == 1U);
    CHECK(stage.response_transmitted());
    CHECK_FALSE(stage.response_acknowledged());
    CHECK(provider.begin_count == 1U);
    CHECK(provider.update_count == 1U);
    CHECK(provider.observed_material_count == 1U);
    CHECK(provider.observed_opaque_byte_count == 16U);
    CHECK(provider.lifetime_releases == 0U);

    const auto events = drain_events(stage);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  resource_response_requirements_ready) == 1U);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  resource_response_ready) == 1U);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  resource_response_queued) == 1U);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  resource_response_transmitted) == 1U);

    const auto sends_after_response = transport.sent.size();
    stage.update(epoch + 5ms);
    stage.update(epoch + 6ms);
    CHECK(transport.sent.size() == sends_after_response);
    CHECK(stage.response_build_count() == 1U);
    CHECK(stage.response_queue_count() == 1U);
    CHECK(response.fragment_payload_size == kExactTempdecalResponse.size());
}

TEST_CASE("Dropped resource response retries the same generation without semantic requeue",
          "[goldsrc][resource-response][stage][loss][retransmission]")
{
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'822U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    goldsrc::ResourceClientResponseStage stage{
        transport,
        remote,
        test_config(),
        &provider};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    deliver_resource_payload(
        stage, transport, remote, driven, epoch + 4ms);
    const auto first_response = require_response_packet(transport);

    // The first response is intentionally not acknowledged. A reliable server
    // packet creates the ordinary outgoing ACK gap, while its wrong-generation
    // ACK cannot release or retry an equal-sequence response.
    transport.queue(
        remote,
        server_packet(
            3U,
            true,
            first_response.header.sequence.sequence.value(),
            false));
    stage.update(epoch + 5ms);
    REQUIRE(transport.sent.size() >= 5U);
    const auto gap_packet = decode_sent(transport.sent.back());
    REQUIRE_FALSE(gap_packet.header.sequence.flags.reliable);
    CHECK(stage.response_queue_count() == 1U);
    CHECK_FALSE(stage.response_acknowledged());

    // Advancing the wrong-generation ACK past the latest response send asks
    // the persistent driver for a transport retry, not a semantic requeue.
    transport.queue(
        remote,
        server_packet(
            4U,
            false,
            gap_packet.header.sequence.sequence.value(),
            false));
    stage.update(epoch + 6ms);
    const auto retry = require_response_packet(transport);
    CHECK(retry.header.sequence.sequence !=
          first_response.header.sequence.sequence);
    REQUIRE(retry.fragments[0U]);
    REQUIRE(first_response.fragments[0U]);
    CHECK(retry.fragments[0U]->packed_id() ==
          first_response.fragments[0U]->packed_id());
    CHECK(retry.payload == first_response.payload);
    CHECK(stage.response_build_count() == 1U);
    CHECK(stage.response_queue_count() == 1U);
    CHECK(stage.response_transmitted());
    CHECK_FALSE(stage.response_acknowledged());

    deliver_covering_continuation(
        stage, transport, remote, 5U, retry, epoch + 7ms);
    REQUIRE(stage.result());
    const auto& lifecycle = stage.result()->reliable_lifecycle();
    CHECK(lifecycle.first_transmit_sequence() ==
          first_response.header.sequence.sequence.value());
    CHECK(lifecycle.most_recent_transmit_sequence() ==
          retry.header.sequence.sequence.value());
    CHECK(lifecycle.transmit_count() == 2U);
    CHECK(lifecycle.reliable_generation() != 0U);
}

TEST_CASE("Dropped ACK waits without duplicate send then accepts one covering ACK",
          "[goldsrc][resource-response][stage][loss][ack]")
{
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'823U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    goldsrc::ResourceClientResponseStage stage{
        transport,
        remote,
        test_config(),
        &provider};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    deliver_resource_payload(
        stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);
    const auto sends_after_response = transport.sent.size();

    // Model a lost server ACK by providing no datagram at all.
    stage.update(epoch + 5ms);
    CHECK(stage.state() ==
          goldsrc::ResourceClientResponseStageState::waiting_for_response_ack);
    CHECK(transport.sent.size() == sends_after_response);
    CHECK(stage.response_queue_count() == 1U);

    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            response.header.sequence.sequence.value(),
            true));
    stage.update(epoch + 6ms);
    CHECK(stage.state() ==
          goldsrc::ResourceClientResponseStageState::
              waiting_for_server_continuation);
    CHECK(stage.response_acknowledged());
    CHECK(stage.response_queue_count() == 1U);
    CHECK_FALSE(stage.result());

    const auto next = next_server_semantic_payload();
    transport.queue(
        remote,
        server_packet(
            4U,
            false,
            response.header.sequence.sequence.value(),
            true,
            service_envelope(next)));
    stage.update(epoch + 7ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->reliable_lifecycle().transmit_count() == 1U);
    CHECK(stage.result()->reliable_lifecycle().acknowledgement().sequence ==
          response.header.sequence.sequence);
}

TEST_CASE("Stage leaves stale ACKs in flight and fails closed on future ACKs",
          "[goldsrc][resource-response][stage][ack][negative]")
{
    SECTION("stale ACK is admitted by transport but cannot release response")
    {
        FakeTransport transport;
        FakeConsistencyProvider provider;
        const auto remote = network::NetworkAddress::loopback(27'824U);
        const auto epoch =
            goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
        goldsrc::ResourceClientResponseStage stage{
            transport,
            remote,
            test_config(),
            &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);
        const auto response = require_response_packet(transport);

        transport.queue(remote, server_packet(3U, false, 2U, true));
        stage.update(epoch + 5ms);
        CHECK(stage.state() ==
              goldsrc::ResourceClientResponseStageState::
                  waiting_for_response_ack);
        CHECK_FALSE(stage.response_acknowledged());
        CHECK(stage.response_queue_count() == 1U);

        deliver_covering_continuation(
            stage, transport, remote, 4U, response, epoch + 6ms);
        REQUIRE(stage.result());
        CHECK(stage.result()->reliable_lifecycle().acknowledgement()
                  .disposition ==
              goldsrc::NetchanAcknowledgementDisposition::advanced);
    }

    SECTION("future ACK terminates through the driver without publication")
    {
        FakeTransport transport;
        FakeConsistencyProvider provider;
        const auto remote = network::NetworkAddress::loopback(27'825U);
        const auto epoch =
            goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
        goldsrc::ResourceClientResponseStage stage{
            transport,
            remote,
            test_config(),
            &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);
        const auto response = require_response_packet(transport);

        transport.queue(
            remote,
            server_packet(
                3U,
                false,
                response.header.sequence.sequence.value() + 1U,
                true));
        stage.update(epoch + 5ms);

        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceClientResponseStageErrorCode::driver_failed);
        REQUIRE(stage.error()->driver_code);
        CHECK(*stage.error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::invalid_acknowledgement);
        CHECK_FALSE(stage.response_acknowledged());
        CHECK_FALSE(stage.result());
        CHECK(stage.cleanup_count() == 1U);
    }
}

TEST_CASE("Covering ACK and BZ2 continuation publish the next server boundary",
          "[goldsrc][resource-response][stage][boundary][bz2]")
{
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'826U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    std::size_t connection_releases = 0U;
    goldsrc::ResourceClientResponseStage stage{
        transport,
        remote,
        test_config(),
        &provider};
    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    deliver_resource_payload(
        stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);
    CHECK(provider.lifetime_releases == 0U);
    CHECK(connection_releases == 0U);

    deliver_covering_continuation(
        stage, transport, remote, 3U, response, epoch + 5ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::
                next_server_boundary_reached);
    REQUIRE(stage.terminal());
    REQUIRE_FALSE(stage.error());
    REQUIRE(stage.result());
    const auto& result = *stage.result();
    CHECK(result.response().wire_name() == "tempdecal.wad");
    CHECK(result.response().byte_count() == 0x01020304U);
    CHECK_FALSE(result.source_carrier_geometry());
    CHECK_FALSE(result.concurrent_tail());
    CHECK(result.boundary().kind() ==
          goldsrc::PostResourceResponseBoundaryKind::opcode_at_payload_start);
    REQUIRE(result.boundary().opcode());
    CHECK(*result.boundary().opcode() == 3U);
    CHECK(result.boundary().byte_offset() == 0U);
    CHECK(result.boundary().remaining_byte_count() == 6U);
    CHECK(result.boundary().source_payload().direction ==
          goldsrc::NetchanDirection::server_to_client);
    CHECK(result.boundary().source_payload().source_sequence == 3U);
    CHECK(result.boundary().source_payload().decompressed);
    CHECK(result.boundary().source_payload().decoded_payload_byte_count == 7U);
    const auto& lifecycle = result.reliable_lifecycle();
    CHECK(lifecycle.fragmented());
    CHECK(lifecycle.fragment_count() == 1U);
    CHECK(lifecycle.transmit_count() == 1U);
    CHECK(lifecycle.first_transmit_sequence() ==
          response.header.sequence.sequence.value());
    CHECK(lifecycle.most_recent_transmit_sequence() ==
          response.header.sequence.sequence.value());
    CHECK(lifecycle.acknowledgement().sequence ==
          response.header.sequence.sequence);
    CHECK(lifecycle.acknowledgement().reliable);
    CHECK(stage.response_acknowledged());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(provider.lifetime_releases == 1U);
    CHECK(connection_releases == 1U);

    const auto events = drain_events(stage);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  resource_response_acknowledged) == 1U);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  server_continuation_received) == 1U);
    CHECK(event_count(
              events,
              goldsrc::ResourceClientResponseStageEventType::
                  next_server_boundary_reached) == 1U);

    const auto sends_at_terminal = transport.sent.size();
    stage.update(epoch + 20ms);
    stage.cancel(epoch + 21ms);
    CHECK(transport.sent.size() == sends_at_terminal);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(provider.lifetime_releases == 1U);
    CHECK(connection_releases == 1U);
}

TEST_CASE("Cancellation is idempotent across provider and owning-session states",
          "[goldsrc][resource-response][stage][cancel][lifetime]")
{
    SECTION("pending provider operation is cancelled exactly once")
    {
        FakeTransport transport;
        FakeConsistencyProvider provider{true};
        const auto remote = network::NetworkAddress::loopback(27'827U);
        const auto epoch =
            goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
        std::size_t connection_releases = 0U;
        goldsrc::ResourceClientResponseStage stage{
            transport,
            remote,
            test_config(),
            &provider};
        const auto driven = drive_to_transition_request(
            stage,
            transport,
            remote,
            epoch,
            std::make_unique<CountingConnectionLifetime>(
                connection_releases));
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);
        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::
                    waiting_for_consistency_provider);
        CHECK(provider.begin_count == 1U);
        CHECK(provider.update_count == 1U);
        CHECK(provider.cancel_count == 0U);
        CHECK(stage.response_queue_count() == 0U);

        stage.cancel(epoch + 5ms);
        stage.cancel(epoch + 6ms);
        stage.update(epoch + 7ms);
        CHECK(stage.state() ==
              goldsrc::ResourceClientResponseStageState::cancelled);
        CHECK(provider.cancel_count == 1U);
        CHECK(provider.lifetime_releases == 0U);
        CHECK(connection_releases == 1U);
        CHECK(stage.cleanup_count() == 1U);
    }

    SECTION("completed provider session is held until stage cancellation")
    {
        FakeTransport transport;
        FakeConsistencyProvider provider;
        const auto remote = network::NetworkAddress::loopback(27'828U);
        const auto epoch =
            goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
        std::size_t connection_releases = 0U;
        goldsrc::ResourceClientResponseStage stage{
            transport,
            remote,
            test_config(),
            &provider};
        const auto driven = drive_to_transition_request(
            stage,
            transport,
            remote,
            epoch,
            std::make_unique<CountingConnectionLifetime>(
                connection_releases));
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);
        static_cast<void>(require_response_packet(transport));
        CHECK(provider.lifetime_releases == 0U);
        CHECK(connection_releases == 0U);

        const auto sends_before_cancel = transport.sent.size();
        stage.cancel(epoch + 5ms);
        stage.cancel(epoch + 6ms);
        CHECK(stage.state() ==
              goldsrc::ResourceClientResponseStageState::cancelled);
        CHECK(transport.sent.size() == sends_before_cancel);
        CHECK(provider.lifetime_releases == 1U);
        CHECK(connection_releases == 1U);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(stage.response_build_count() == 1U);
        CHECK(stage.response_queue_count() == 1U);
    }
}

TEST_CASE("Provider wait and synchronous failure remain bounded and redacted",
          "[goldsrc][resource-response][stage][provider][security]")
{
    const auto remote = network::NetworkAddress::loopback(27'828U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 8s;

    SECTION("pending provider reaches an independent manual-clock deadline")
    {
        FakeTransport transport;
        FakeConsistencyProvider provider{true};
        auto config = test_config();
        config.consistency_provider_timeout = 5ms;
        goldsrc::ResourceClientResponseStage stage{
            transport, remote, config, &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);
        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::
                    waiting_for_consistency_provider);

        stage.update(epoch + 9ms);

        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::timed_out);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceClientResponseStageErrorCode::
                  consistency_provider_failed);
        REQUIRE(stage.error()->consistency_code);
        CHECK(*stage.error()->consistency_code ==
              consistency::ResourceConsistencyErrorCode::timed_out);
        CHECK(provider.cancel_count == 1U);
        CHECK(stage.response_build_count() == 0U);
        CHECK(stage.response_queue_count() == 0U);
    }

    SECTION("provider diagnostics cannot cross into the CLI-visible error")
    {
        FakeTransport transport;
        SensitiveFailureProvider provider;
        goldsrc::ResourceClientResponseStage stage{
            transport, remote, test_config(), &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);

        REQUIRE(stage.error());
        CHECK(provider.begin_count == 1U);
        CHECK(stage.error()->context.find("tempdecal") == std::string::npos);
        CHECK(stage.error()->context.find("a0a1") == std::string::npos);
        CHECK(stage.response_queue_count() == 0U);
    }
}

TEST_CASE("Response-stage bounds reject partial publication and stale continuation",
          "[goldsrc][resource-response][stage][bounds][security]")
{
    SECTION("provider deadline validates its exact hard cap")
    {
        auto config = test_config();
        config.consistency_provider_timeout =
            goldsrc::kMaximumResourceConsistencyProviderTimeout;
        CHECK(goldsrc::valid_resource_client_response_stage_configuration(
            config));
        config.consistency_provider_timeout =
            goldsrc::kMaximumResourceConsistencyProviderTimeout + 1ms;
        CHECK_FALSE(
            goldsrc::valid_resource_client_response_stage_configuration(
                config));
        config.consistency_provider_timeout = 0ms;
        CHECK_FALSE(
            goldsrc::valid_resource_client_response_stage_configuration(
                config));
    }

    SECTION("response ACK and post-ACK deadlines validate their exact hard caps")
    {
        auto config = test_config();
        config.response_acknowledgement_timeout =
            goldsrc::kMaximumResourceResponseAcknowledgementTimeout;
        config.post_ack_boundary_timeout =
            goldsrc::kMaximumPostResourceResponseBoundaryTimeout;
        CHECK(goldsrc::valid_resource_client_response_stage_configuration(
            config));

        config.response_acknowledgement_timeout =
            goldsrc::kMaximumResourceResponseAcknowledgementTimeout + 1ms;
        CHECK_FALSE(
            goldsrc::valid_resource_client_response_stage_configuration(
                config));
        config.response_acknowledgement_timeout =
            goldsrc::kMaximumResourceResponseAcknowledgementTimeout;
        config.post_ack_boundary_timeout =
            goldsrc::kMaximumPostResourceResponseBoundaryTimeout + 1ms;
        CHECK_FALSE(
            goldsrc::valid_resource_client_response_stage_configuration(
                config));

        config.post_ack_boundary_timeout =
            goldsrc::kMaximumPostResourceResponseBoundaryTimeout;
        config.response_acknowledgement_timeout = 0ms;
        CHECK_FALSE(
            goldsrc::valid_resource_client_response_stage_configuration(
                config));
        config.response_acknowledgement_timeout = 1ms;
        config.post_ack_boundary_timeout = 0ms;
        CHECK_FALSE(
            goldsrc::valid_resource_client_response_stage_configuration(
                config));
    }

    SECTION("one and three event slots retain terminal backpressure publication")
    {
        for (const auto capacity : {1U, 3U}) {
            FakeTransport transport;
            FakeConsistencyProvider provider;
            const auto remote = network::NetworkAddress::loopback(
                static_cast<std::uint16_t>(27'829U + capacity));
            const auto epoch =
                goldsrc::ResourceClientResponseStageTimePoint{} + 9s;
            auto config = test_config();
            config.response.maximum_response_stage_events = capacity;
            goldsrc::ResourceClientResponseStage stage{
                transport, remote, config, &provider};
            const auto driven = drive_to_transition_request(
                stage, transport, remote, epoch);
            deliver_resource_payload(
                stage, transport, remote, driven, epoch + 4ms);

            REQUIRE(stage.state() ==
                    goldsrc::ResourceClientResponseStageState::backpressure);
            REQUIRE(stage.error());
            CHECK(stage.error()->code ==
                  goldsrc::ResourceClientResponseStageErrorCode::
                      event_backpressure);
            const auto events = drain_events(stage);
            CHECK(event_count(
                      events,
                      goldsrc::ResourceClientResponseStageEventType::
                          backpressure) == 1U);
            CHECK(event_count(
                      events,
                      goldsrc::ResourceClientResponseStageEventType::
                          resource_response_requirements_ready) ==
                  (capacity == 1U ? 0U : 1U));
            CHECK(stage.response_build_count() == 0U);
            CHECK(stage.response_queue_count() == 0U);
        }
    }

    SECTION("unknown provider update state fails closed")
    {
        FakeTransport transport;
        InvalidUpdateProvider provider;
        const auto remote = network::NetworkAddress::loopback(27'833U);
        const auto epoch =
            goldsrc::ResourceClientResponseStageTimePoint{} + 9s;
        goldsrc::ResourceClientResponseStage stage{
            transport, remote, test_config(), &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);

        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceClientResponseStageErrorCode::
                  consistency_provider_result_invalid);
        CHECK(provider.begin_count == 1U);
        CHECK(provider.update_count == 1U);
        CHECK(provider.cancel_count == 1U);
        CHECK(stage.response_build_count() == 0U);
        CHECK(stage.response_queue_count() == 0U);
    }

    SECTION("payload already queued before first response TX is rejected")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'830U);
        const auto epoch =
            goldsrc::ResourceClientResponseStageTimePoint{} + 10s;
        FakeConsistencyProvider provider;
        goldsrc::ResourceClientResponseStage stage{
            transport, remote, test_config(), &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        transport.queue(
            remote,
            server_packet(
                2U,
                false,
                driven.request.header.sequence.sequence.value(),
                false,
                service_envelope(resource_semantic_payload())));
        transport.queue(
            remote,
            server_packet(
                3U,
                false,
                driven.request.header.sequence.sequence.value(),
                false,
                service_envelope(next_server_semantic_payload())));

        stage.update(epoch + 4ms);

        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceClientResponseStageErrorCode::
                  server_payload_before_response_transmit);
        CHECK(stage.response_queue_count() == 1U);
        CHECK_FALSE(stage.response_transmitted());
    }
}

TEST_CASE("Independent response-boundary deadlines ignore header-only traffic",
          "[goldsrc][resource-response][stage][timeout][manual-clock]")
{
    const auto epoch =
        goldsrc::ResourceClientResponseStageTimePoint{} + 11s;

    SECTION("header-only traffic cannot extend response ACK deadline")
    {
        FakeTransport transport;
        FakeConsistencyProvider provider;
        const auto remote = network::NetworkAddress::loopback(27'834U);
        auto config = test_config();
        config.response_acknowledgement_timeout = 5ms;
        goldsrc::ResourceClientResponseStage stage{
            transport, remote, config, &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);
        const auto response = require_response_packet(transport);
        REQUIRE(response.header.sequence.sequence.value() > 2U);

        for (std::uint32_t incoming_sequence = 3U;
             incoming_sequence <= 6U;
             ++incoming_sequence) {
            transport.queue(
                remote,
                server_packet(incoming_sequence, false, 2U, true));
            stage.update(
                epoch + std::chrono::milliseconds{incoming_sequence + 2U});
            REQUIRE(stage.state() ==
                    goldsrc::ResourceClientResponseStageState::
                        waiting_for_response_ack);
        }

        stage.update(epoch + 9ms);

        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::timed_out);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceClientResponseStageErrorCode::
                  response_acknowledgement_timed_out);
        CHECK_FALSE(stage.response_acknowledged());
        CHECK(stage.response_queue_count() == 1U);
    }

    SECTION("header-only traffic cannot extend post-ACK boundary deadline")
    {
        FakeTransport transport;
        FakeConsistencyProvider provider;
        const auto remote = network::NetworkAddress::loopback(27'835U);
        auto config = test_config();
        config.post_ack_boundary_timeout = 5ms;
        goldsrc::ResourceClientResponseStage stage{
            transport, remote, config, &provider};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms);
        const auto response = require_response_packet(transport);

        transport.queue(
            remote,
            server_packet(
                3U,
                false,
                response.header.sequence.sequence.value(),
                true));
        stage.update(epoch + 5ms);
        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::
                    waiting_for_server_continuation);

        for (std::uint32_t incoming_sequence = 4U;
             incoming_sequence <= 7U;
             ++incoming_sequence) {
            transport.queue(
                remote,
                server_packet(
                    incoming_sequence,
                    false,
                    response.header.sequence.sequence.value(),
                    true));
            stage.update(
                epoch + std::chrono::milliseconds{incoming_sequence + 2U});
            REQUIRE(stage.state() ==
                    goldsrc::ResourceClientResponseStageState::
                        waiting_for_server_continuation);
        }

        stage.update(epoch + 10ms);

        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::timed_out);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceClientResponseStageErrorCode::
                  post_response_boundary_timed_out);
        CHECK(stage.response_acknowledged());
        CHECK_FALSE(stage.result());
    }
}

TEST_CASE("Delayed continuation must acknowledge at least the first response TX",
          "[goldsrc][resource-response][stage][ack][security]")
{
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'836U);
    const auto epoch =
        goldsrc::ResourceClientResponseStageTimePoint{} + 12s;
    goldsrc::ResourceClientResponseStage stage{
        transport, remote, test_config(), &provider};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch);
    deliver_resource_payload(
        stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);
    REQUIRE(response.header.sequence.sequence.value() > 0U);

    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            response.header.sequence.sequence.value() - 1U,
            true,
            service_envelope(next_server_semantic_payload())));
    stage.update(epoch + 5ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceClientResponseStageErrorCode::
              server_payload_acknowledgement_invalid);
    CHECK_FALSE(stage.response_acknowledged());
    CHECK_FALSE(stage.result());
    CHECK(stage.response_queue_count() == 1U);
}

TEST_CASE("Historical resource-list stop remains zero-queue and zero-send at its boundary",
          "[goldsrc][resource-list][stage][regression][zero-send]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'829U);
    const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
    std::size_t connection_releases = 0U;
    goldsrc::ResourceListStage stage{
        transport,
        remote,
        test_config().resource_list};
    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    const auto sends_before_boundary = transport.sent.size();

    deliver_resource_payload(
        stage, transport, remote, driven, epoch + 4ms);
    REQUIRE(stage.state() ==
            goldsrc::ResourceListStageState::client_response_required);
    REQUIRE(stage.result());
    CHECK(stage.response_queue_count() == 0U);
    CHECK(transport.sent.size() == sends_before_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(connection_releases == 1U);

    stage.update(epoch + 5ms);
    stage.cancel(epoch + 6ms);
    CHECK(stage.response_queue_count() == 0U);
    CHECK(transport.sent.size() == sends_before_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(connection_releases == 1U);
}

} // namespace
