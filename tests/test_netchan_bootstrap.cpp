#include <hlclient/goldsrc/netchan_bootstrap_stage.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

class FakeDatagramTransport final : public network::IDatagramTransport {
public:
    struct SentDatagram {
        network::NetworkAddress destination;
        std::vector<std::byte> payload;
    };

    [[nodiscard]] network::DatagramLocalAddressResult local_address() const override
    {
        ++local_address_queries;
        if (throw_on_local_address) {
            throw std::runtime_error{"synthetic local endpoint exception"};
        }
        return network::DatagramLocalAddressResult{local, local_error};
    }

    [[nodiscard]] network::DatagramSendResult send_to(
        const network::NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        ++send_attempt_count;
        sent.push_back(SentDatagram{
            destination,
            std::vector<std::byte>{payload.begin(), payload.end()},
        });
        if (throw_on_send) {
            throw std::runtime_error{"synthetic send exception"};
        }
        if (!send_results.empty()) {
            auto result = std::move(send_results.front());
            send_results.pop_front();
            return result;
        }
        return network::DatagramSendResult{network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        const std::size_t maximum_size) override
    {
        ++receive_count;
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

    void queue_truncated(
        const network::NetworkAddress source,
        const std::size_t lower_bound)
    {
        incoming.push_back(network::DatagramTransportReceiveResult{
            network::DatagramTransportReceiveStatus::truncated,
            std::nullopt,
            source,
            lower_bound,
            "synthetic truncation",
        });
    }

    std::optional<network::NetworkAddress> local{
        network::NetworkAddress::loopback(30'000)};
    std::string local_error;
    mutable bool throw_on_local_address{false};
    bool throw_on_send{false};
    bool throw_on_receive{false};
    mutable std::size_t local_address_queries{0U};
    std::size_t send_attempt_count{0U};
    std::size_t receive_count{0U};
    std::vector<std::size_t> receive_limits;
    std::deque<network::DatagramSendResult> send_results;
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
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
    std::ranges::transform(values, std::back_inserter(output), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return output;
}

template<std::size_t Size>
[[nodiscard]] std::vector<std::byte> bytes(
    const std::array<std::uint8_t, Size>& values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(values, std::back_inserter(output), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return output;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    std::vector<std::byte> payload,
    const bool reliable = false,
    const bool reliable_acknowledgement = false)
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

void append_uint16_le(std::vector<std::byte>& output, const std::uint16_t value)
{
    output.push_back(std::byte{static_cast<std::uint8_t>(value & 0xffU)});
    output.push_back(std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xffU)});
}

void append_uint32_le(std::vector<std::byte>& output, const std::uint32_t value)
{
    output.push_back(std::byte{static_cast<std::uint8_t>(value & 0xffU)});
    output.push_back(std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xffU)});
    output.push_back(std::byte{static_cast<std::uint8_t>((value >> 16U) & 0xffU)});
    output.push_back(std::byte{static_cast<std::uint8_t>((value >> 24U) & 0xffU)});
}

[[nodiscard]] std::vector<std::byte> fragmented_packet(
    const std::size_t slot,
    const std::uint32_t acknowledgement = 0U)
{
    if (slot >= goldsrc::kNetchanFragmentSlotCount) {
        throw std::runtime_error{"invalid synthetic fragment slot"};
    }

    std::vector<std::byte> decoded_body;
    for (std::size_t current = 0U; current < goldsrc::kNetchanFragmentSlotCount;
         ++current) {
        decoded_body.push_back(std::byte{current == slot ? 1U : 0U});
        if (current == slot) {
            append_uint32_le(decoded_body, 0x0001'0001U);
            append_uint16_le(decoded_body, 0U);
            append_uint16_le(decoded_body, 4U);
        }
    }
    const auto fragment = bytes({0x10U, 0x20U, 0x30U, 0x40U});
    decoded_body.insert(decoded_body.end(), fragment.begin(), fragment.end());
    goldsrc::encode_netchan_payload(decoded_body, sequence(1U));

    std::vector<std::byte> datagram;
    append_uint32_le(
        datagram,
        goldsrc::kNetchanReliableSequenceFlag |
            goldsrc::kNetchanFragmentSequenceFlag | 1U);
    append_uint32_le(datagram, acknowledgement);
    datagram.insert(datagram.end(), decoded_body.begin(), decoded_body.end());
    return datagram;
}

[[nodiscard]] std::vector<std::byte> ignored_header_only_packet(
    const std::uint32_t numeric_sequence)
{
    std::vector<std::byte> datagram;
    append_uint32_le(datagram, numeric_sequence);
    // Reserved ACK bit and malformed body prove duplicate/old classification
    // happens before strict body decoding.
    append_uint32_le(datagram, goldsrc::kNetchanReservedAcknowledgementFlag);
    datagram.push_back(std::byte{0xff});
    return datagram;
}

[[nodiscard]] goldsrc::NetchanBootstrapConfig test_config()
{
    goldsrc::NetchanBootstrapConfig config;
    config.first_packet_timeout = 100ms;
    config.maximum_datagram_size = goldsrc::kDefaultNetchanDatagramSize;
    config.maximum_datagrams_per_update = 8U;
    config.maximum_outgoing_packets_per_update = 1U;
    config.maximum_opaque_payload_size =
        goldsrc::kDefaultNetchanBootstrapOpaquePayloadSize;
    return config;
}

[[nodiscard]] const std::array<std::uint8_t, 16U>& exact_first_acknowledgement()
{
    // Independent literal: sequence 1, acknowledgement 1 with reliable ACK,
    // followed by eight transformed 0x01 padding bytes under sequence key 1.
    static constexpr std::array<std::uint8_t, 16U> fixture{
        0x01U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x80U,
        0x5aU, 0x19U, 0x01U, 0x00U,
        0x1aU, 0x01U, 0x11U, 0x40U,
    };
    return fixture;
}

void require_started(
    goldsrc::NetchanBootstrapStage& stage,
    const goldsrc::NetchanBootstrapTimePoint now,
    const network::NetworkAddress local)
{
    REQUIRE(stage.start(now, local));
    REQUIRE(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
    REQUIRE_FALSE(stage.terminal());
}

void check_error(
    const goldsrc::NetchanBootstrapStage& stage,
    const goldsrc::NetchanBootstrapState state,
    const goldsrc::NetchanBootstrapErrorCode code)
{
    CHECK(stage.state() == state);
    CHECK(stage.terminal());
    REQUIRE(stage.error());
    CHECK(stage.error()->code == code);
    CHECK_FALSE(stage.error()->context.empty());
    CHECK(stage.error()->context.size() <= goldsrc::kNetchanBootstrapDiagnosticTextLimit);
    CHECK_FALSE(stage.result());
}

TEST_CASE("M2.3.1 bootstrap start validates same socket and sends nothing",
          "[goldsrc][netchan][bootstrap]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};

    require_started(stage, epoch, *transport.local);
    CHECK(stage.local_endpoint() == transport.local);
    REQUIRE(stage.first_packet_deadline());
    CHECK(*stage.first_packet_deadline() == epoch + 100ms);
    CHECK(transport.sent.empty());
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK_FALSE(stage.session().first_incoming_committed());
    CHECK_FALSE(stage.session().first_acknowledgement_prepared());
    CHECK_FALSE(stage.session().first_acknowledgement_sent());
}

TEST_CASE("M2.3.1 bootstrap configuration has finite hard bounds",
          "[goldsrc][netchan][bootstrap][configuration]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    const auto check_invalid = [&](const goldsrc::NetchanBootstrapConfig config) {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, config};
        CHECK_FALSE(stage.start(epoch, *transport.local));
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::protocol_error,
            goldsrc::NetchanBootstrapErrorCode::invalid_configuration);
        CHECK(transport.sent.empty());
    };

    auto config = test_config();
    config.first_packet_timeout = 0ms;
    check_invalid(config);
    config = test_config();
    config.first_packet_timeout = goldsrc::kMaximumNetchanBootstrapTimeout + 1ms;
    check_invalid(config);
    config = test_config();
    config.maximum_datagram_size = goldsrc::kMinimumNetchanBootstrapDatagramSize - 1U;
    config.maximum_opaque_payload_size =
        config.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    check_invalid(config);
    config = test_config();
    config.maximum_datagram_size = goldsrc::kMaximumNetchanDatagramSize + 1U;
    check_invalid(config);
    config = test_config();
    config.maximum_datagrams_per_update = 0U;
    check_invalid(config);
    config = test_config();
    config.maximum_datagrams_per_update =
        goldsrc::kMaximumNetchanBootstrapDatagramsPerUpdate + 1U;
    check_invalid(config);
    config = test_config();
    config.maximum_outgoing_packets_per_update = 0U;
    check_invalid(config);
    config = test_config();
    config.maximum_opaque_payload_size = 0U;
    check_invalid(config);
    config = test_config();
    config.maximum_opaque_payload_size = config.maximum_datagram_size;
    check_invalid(config);

    SECTION("minimum 16-byte datagram budget is valid")
    {
        FakeDatagramTransport transport;
        config = test_config();
        config.maximum_datagram_size = goldsrc::kMinimumNetchanBootstrapDatagramSize;
        config.maximum_opaque_payload_size =
            goldsrc::kStockProtocol48MinimumDecodedPayloadSize;
        goldsrc::NetchanBootstrapStage stage{transport, remote, config};
        CHECK(stage.start(epoch, *transport.local));
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
        CHECK(transport.sent.empty());
    }
}

TEST_CASE("M2.3.1 bootstrap rejects unavailable or mismatched local endpoints",
          "[goldsrc][netchan][bootstrap][endpoint]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;

    SECTION("query error")
    {
        FakeDatagramTransport transport;
        transport.local.reset();
        transport.local_error = "synthetic endpoint failure";
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        CHECK_FALSE(stage.start(epoch, network::NetworkAddress::loopback(30'000)));
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::network_error,
            goldsrc::NetchanBootstrapErrorCode::local_endpoint_unavailable);
    }

    SECTION("query exception")
    {
        FakeDatagramTransport transport;
        transport.throw_on_local_address = true;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        CHECK_FALSE(stage.start(epoch, network::NetworkAddress::loopback(30'000)));
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::network_error,
            goldsrc::NetchanBootstrapErrorCode::local_endpoint_unavailable);
    }

    SECTION("different endpoint")
    {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        CHECK_FALSE(stage.start(epoch, network::NetworkAddress::loopback(30'001)));
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::network_error,
            goldsrc::NetchanBootstrapErrorCode::local_endpoint_changed);
    }
}

TEST_CASE("M2.3.1 bootstrap detects local endpoint drift before receive",
          "[goldsrc][netchan][bootstrap][endpoint]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);

    transport.local = network::NetworkAddress::loopback(30'001);
    stage.update(epoch + 1ms);
    check_error(
        stage,
        goldsrc::NetchanBootstrapState::network_error,
        goldsrc::NetchanBootstrapErrorCode::local_endpoint_changed);
    CHECK(transport.receive_count == 0U);
}

TEST_CASE("M2.3.1 bootstrap would-block wait is nonterminal and quiet",
          "[goldsrc][netchan][bootstrap][poll]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);

    stage.update(epoch + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
    CHECK_FALSE(stage.terminal());
    CHECK(transport.receive_count == 1U);
    CHECK(transport.sent.empty());
}

TEST_CASE("M2.3.1 bootstrap owns opaque metadata and sends exact first ACK once",
          "[goldsrc][netchan][bootstrap][ack]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);

    const auto payload = bytes({0x4eU, 0x45U, 0x54U, 0x43U, 0x48U, 0x41U, 0x4eU});
    transport.queue(remote, server_packet(1U, 0U, payload, true));
    stage.update(epoch + 1ms);

    REQUIRE(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes == payload);
    CHECK(stage.result()->payload.source_sequence.value() == 1U);
    CHECK(stage.result()->payload.source_acknowledgement.value() == 0U);
    CHECK(stage.result()->payload.sequence_flags.reliable);
    CHECK_FALSE(stage.result()->payload.sequence_flags.fragmented);
    CHECK_FALSE(stage.result()->payload.acknowledgement_reliable);
    CHECK(stage.result()->payload.direction == goldsrc::NetchanDirection::server_to_client);
    CHECK(stage.result()->payload.received_at == epoch + 1ms);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent[0].destination == remote);
    CHECK(transport.sent[0].payload == bytes(exact_first_acknowledgement()));
    CHECK(stage.transmitted_packet_count() == 1U);
    CHECK(stage.session().first_incoming_committed());
    CHECK(stage.session().first_acknowledgement_sent());

    stage.update(epoch + 2ms);
    stage.cancel(epoch + 3ms);
    CHECK(transport.sent.size() == 1U);
    CHECK(stage.result()->payload.bytes == payload);
}

TEST_CASE("M2.3.1 bootstrap preserves empty opaque payload without interpretation",
          "[goldsrc][netchan][bootstrap][opaque]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue(remote, server_packet(1U, 0U, {}, false));

    stage.update(epoch + 1ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes.empty());
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("M2.3.1 bootstrap accepts the exact configured opaque payload bound",
          "[goldsrc][netchan][bootstrap][bounds]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_datagram_size = 64U;
    config.maximum_opaque_payload_size = 16U;
    goldsrc::NetchanBootstrapStage stage{transport, remote, config};
    require_started(stage, epoch, *transport.local);
    const std::vector<std::byte> payload(16U, std::byte{0x41});
    transport.queue(remote, server_packet(1U, 0U, payload));

    stage.update(epoch + 1ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes == payload);
}

TEST_CASE("M2.3.1 bootstrap rejects opaque payload bound plus one before ACK",
          "[goldsrc][netchan][bootstrap][bounds]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_datagram_size = 64U;
    config.maximum_opaque_payload_size = 16U;
    goldsrc::NetchanBootstrapStage stage{transport, remote, config};
    require_started(stage, epoch, *transport.local);
    transport.queue(remote, server_packet(1U, 0U, std::vector<std::byte>(17U)));

    stage.update(epoch + 1ms);
    check_error(
        stage,
        goldsrc::NetchanBootstrapState::protocol_error,
        goldsrc::NetchanBootstrapErrorCode::opaque_payload_too_large);
    CHECK(transport.sent.empty());
    CHECK_FALSE(stage.session().first_incoming_committed());
}

TEST_CASE("M2.3.1 duplicate is ignored before strict body decode",
          "[goldsrc][netchan][bootstrap][duplicate]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue(remote, ignored_header_only_packet(0U));
    transport.queue(remote, server_packet(1U, 0U, bytes({0x42U})));

    stage.update(epoch + 1ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes == bytes({0x42U}));
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("M2.3.1 old packet is ignored before strict body decode",
          "[goldsrc][netchan][bootstrap][reorder]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue(
        remote,
        ignored_header_only_packet(goldsrc::kNetchanSequenceMask - 2U));
    transport.queue(remote, server_packet(1U, 0U, bytes({0x43U})));

    stage.update(epoch + 1ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes == bytes({0x43U}));
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("M2.3.1 wrong endpoint is ignored before parsing",
          "[goldsrc][netchan][bootstrap][endpoint]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto wrong = network::NetworkAddress::loopback(27'129);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue(wrong, bytes({0xffU}));
    transport.queue(remote, server_packet(1U, 0U, bytes({0x44U})));

    stage.update(epoch + 1ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes == bytes({0x44U}));
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("M2.3.1 wrong-endpoint truncation cannot terminate the real session",
          "[goldsrc][netchan][bootstrap][endpoint][bounds]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto wrong = network::NetworkAddress::loopback(27'129);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue_truncated(wrong, goldsrc::kMaximumNetchanDatagramSize + 1U);
    transport.queue(remote, server_packet(1U, 0U, bytes({0x45U})));

    stage.update(epoch + 1ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes == bytes({0x45U}));
}

TEST_CASE("M2.3.1 target connectionless special and malformed packets are typed",
          "[goldsrc][netchan][bootstrap][protocol]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    const auto check_packet = [&](std::vector<std::byte> packet,
                                  const goldsrc::NetchanBootstrapErrorCode code) {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        require_started(stage, epoch, *transport.local);
        transport.queue(remote, std::move(packet));
        stage.update(epoch + 1ms);
        check_error(stage, goldsrc::NetchanBootstrapState::protocol_error, code);
        CHECK(transport.sent.empty());
    };

    check_packet(
        bytes({0xffU, 0xffU, 0xffU, 0xffU, 0x42U}),
        goldsrc::NetchanBootstrapErrorCode::unexpected_connectionless_packet);
    check_packet(
        bytes({0xfeU, 0xffU, 0xffU, 0xffU, 0U, 0U, 0U, 0U}),
        goldsrc::NetchanBootstrapErrorCode::unsupported_special_packet);
    check_packet(
        bytes({0U, 0U, 0U, 0U, 0U, 0U, 0U}),
        goldsrc::NetchanBootstrapErrorCode::malformed_packet);
}

TEST_CASE("M2.3.1 impossible acknowledgement and half-range sequence are typed",
          "[goldsrc][netchan][bootstrap][sequence]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;

    SECTION("future acknowledgement")
    {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        require_started(stage, epoch, *transport.local);
        transport.queue(remote, server_packet(1U, 1U, bytes({0x41U})));
        stage.update(epoch + 1ms);
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::protocol_error,
            goldsrc::NetchanBootstrapErrorCode::invalid_acknowledgement);
    }

    SECTION("half range")
    {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        require_started(stage, epoch, *transport.local);
        transport.queue(
            remote,
            server_packet(goldsrc::kNetchanSequenceHalfRange, 0U, bytes({0x41U})));
        stage.update(epoch + 1ms);
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::protocol_error,
            goldsrc::NetchanBootstrapErrorCode::invalid_sequence);
    }
}

TEST_CASE("M2.3.1 slot-zero fragment is a typed pending boundary without ACK",
          "[goldsrc][netchan][bootstrap][fragment][pending]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue(remote, fragmented_packet(0U));

    stage.update(epoch + 1ms);
    check_error(
        stage,
        goldsrc::NetchanBootstrapState::fragmented_payload_pending_m2_3_3,
        goldsrc::NetchanBootstrapErrorCode::fragmented_payload_pending_m2_3_3);
    CHECK(transport.sent.empty());
    CHECK_FALSE(stage.session().first_incoming_committed());
    CHECK(stage.session().state().incoming_sequence.value() == 0U);
}

TEST_CASE("M2.3.1 slot-one fragment has the same typed pending boundary",
          "[goldsrc][netchan][bootstrap][fragment][pending]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue(remote, fragmented_packet(1U));

    stage.update(epoch + 1ms);
    check_error(
        stage,
        goldsrc::NetchanBootstrapState::fragmented_payload_pending_m2_3_3,
        goldsrc::NetchanBootstrapErrorCode::fragmented_payload_pending_m2_3_3);
    CHECK(transport.sent.empty());
    CHECK_FALSE(stage.session().first_incoming_committed());
}

TEST_CASE("Fragment pending takes precedence over reliable ACK validation",
          "[goldsrc][netchan][bootstrap][fragment][pending][reliable]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    // ACK 1 is impossible before the first project transmission, but a valid
    // fragmented packet is intentionally stopped at the M2.3.3 boundary
    // before ACK/reliable lifecycle processing.
    transport.queue(remote, fragmented_packet(0U, 1U));

    stage.update(epoch + 1ms);
    check_error(
        stage,
        goldsrc::NetchanBootstrapState::fragmented_payload_pending_m2_3_3,
        goldsrc::NetchanBootstrapErrorCode::fragmented_payload_pending_m2_3_3);
    CHECK(transport.sent.empty());
    CHECK_FALSE(stage.session().first_incoming_committed());
    CHECK(stage.session().state().incoming_sequence.value() == 0U);
    CHECK(stage.persistent_session() == nullptr);
}

TEST_CASE("M2.3.1 first-packet timeout is deterministic and terminal",
          "[goldsrc][netchan][bootstrap][timeout]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.queue(remote, server_packet(1U, 0U, bytes({0x41U})));

    stage.update(epoch + 100ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::timed_out);
    CHECK(stage.terminal());
    CHECK(transport.receive_count == 0U);
    CHECK(transport.sent.empty());
}

TEST_CASE("M2.3.1 cancellation is terminal and idempotent",
          "[goldsrc][netchan][bootstrap][cancel]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    stage.cancel(epoch + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::cancelled);
    CHECK(stage.terminal());

    stage.update(epoch + 2ms);
    stage.cancel(epoch + 3ms);
    CHECK(transport.receive_count == 0U);
    CHECK(transport.sent.empty());
}

TEST_CASE("M2.3.1 receive failures and inconsistent metadata are typed",
          "[goldsrc][netchan][bootstrap][network]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;

    SECTION("receive exception")
    {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        require_started(stage, epoch, *transport.local);
        transport.throw_on_receive = true;
        stage.update(epoch + 1ms);
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::network_error,
            goldsrc::NetchanBootstrapErrorCode::receive_failed);
    }

    SECTION("inconsistent would block")
    {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        require_started(stage, epoch, *transport.local);
        transport.incoming.push_back(network::DatagramTransportReceiveResult{
            network::DatagramTransportReceiveStatus::would_block,
            std::nullopt,
            remote,
            1U,
            {},
        });
        stage.update(epoch + 1ms);
        check_error(
            stage,
            goldsrc::NetchanBootstrapState::network_error,
            goldsrc::NetchanBootstrapErrorCode::inconsistent_receive_result);
    }
}

TEST_CASE("M2.3.1 ACK send failure withholds result and never retries",
          "[goldsrc][netchan][bootstrap][ack][network]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    transport.send_results.push_back(network::DatagramSendResult{
        network::DatagramSendStatus::error,
        "synthetic ACK failure",
    });
    transport.queue(remote, server_packet(1U, 0U, bytes({0x41U}), true));

    stage.update(epoch + 1ms);
    check_error(
        stage,
        goldsrc::NetchanBootstrapState::network_error,
        goldsrc::NetchanBootstrapErrorCode::send_failed);
    CHECK(transport.send_attempt_count == 1U);
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(stage.session().first_incoming_committed());
    CHECK(stage.session().first_acknowledgement_prepared());
    CHECK_FALSE(stage.session().first_acknowledgement_sent());

    stage.update(epoch + 2ms);
    CHECK(transport.send_attempt_count == 1U);
}

TEST_CASE("M2.3.1 receive and transmit work stay bounded per update",
          "[goldsrc][netchan][bootstrap][bounds]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    auto config = test_config();
    config.maximum_datagrams_per_update = 2U;
    goldsrc::NetchanBootstrapStage stage{transport, remote, config};
    require_started(stage, epoch, *transport.local);
    transport.queue(remote, ignored_header_only_packet(0U));
    transport.queue(remote, ignored_header_only_packet(0U));
    transport.queue(remote, server_packet(1U, 0U, bytes({0x41U}), true));

    stage.update(epoch + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
    CHECK(transport.receive_count == 2U);
    CHECK(transport.sent.empty());

    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    CHECK(transport.receive_count == 3U);
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("M2.3.1 opaque svc-looking bytes are never interpreted",
          "[goldsrc][netchan][bootstrap][opaque][signon]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    require_started(stage, epoch, *transport.local);
    const auto svc_looking = bytes({0x0bU, 0xffU, 0x00U, 0x07U, 0x1dU});
    transport.queue(remote, server_packet(1U, 0U, svc_looking));

    stage.update(epoch + 1ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->payload.bytes == svc_looking);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
}

TEST_CASE("M2.3.1 trace is metadata-only reentrancy-safe and exception-isolated",
          "[goldsrc][netchan][bootstrap][trace]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{} + 1s;
    std::vector<goldsrc::NetchanBootstrapTraceEvent> events;
    goldsrc::NetchanBootstrapStage* stage_pointer = nullptr;
    goldsrc::NetchanBootstrapStage stage{
        transport,
        remote,
        test_config(),
        [&](const goldsrc::NetchanBootstrapTraceEvent& event) {
            events.push_back(event);
            if (stage_pointer != nullptr) {
                stage_pointer->update(epoch + 1ms);
            }
            throw std::runtime_error{"synthetic trace exception"};
        }};
    stage_pointer = &stage;

    require_started(stage, epoch, *transport.local);
    transport.queue(remote, server_packet(1U, 0U, bytes({0x41U}), true));
    stage.update(epoch + 2ms);
    REQUIRE(stage.result());
    CHECK(transport.sent.size() == 1U);
    CHECK_FALSE(events.empty());
    CHECK(events.back().transmitted_packet_count == 1U);
}

} // namespace
