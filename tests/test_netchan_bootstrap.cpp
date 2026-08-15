#include <hlclient/goldsrc/netchan_bootstrap_stage.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
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
        if (!local_error.empty()) {
            return network::DatagramLocalAddressResult{std::nullopt, local_error};
        }
        return network::DatagramLocalAddressResult{local, {}};
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

    network::NetworkAddress local{network::NetworkAddress::loopback(30'000)};
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

[[nodiscard]] bool equal_bytes(
    const std::span<const std::byte> left,
    const std::span<const std::byte> right)
{
    return std::ranges::equal(left, right);
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

[[nodiscard]] std::vector<std::byte> normal_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const std::uint16_t one_based_index,
    const std::uint16_t count,
    std::vector<std::byte> fragment,
    const bool reliable = true)
{
    if (fragment.size() > 0xffffU) {
        throw std::runtime_error{"synthetic fragment is too large"};
    }
    goldsrc::NetchanFragmentSlots slots;
    slots[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        (static_cast<std::uint32_t>(one_based_index) << 16U) |
            static_cast<std::uint32_t>(count),
        0U,
        static_cast<std::uint16_t>(fragment.size()),
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{reliable, true},
            },
            goldsrc::NetchanAcknowledgementWord{sequence(acknowledgement), false},
        },
        std::move(slots),
        std::move(fragment),
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"unable to encode synthetic normal fragment"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> secondary_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement)
{
    auto payload = bytes({0x21U, 0x22U});
    goldsrc::NetchanFragmentSlots slots;
    slots[1U] = goldsrc::NetchanFragmentDescriptor{
        1U,
        0x0001'0001U,
        0U,
        static_cast<std::uint16_t>(payload.size()),
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{true, true},
            },
            goldsrc::NetchanAcknowledgementWord{sequence(acknowledgement), false},
        },
        std::move(slots),
        std::move(payload),
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"unable to encode synthetic secondary fragment"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::NetchanBootstrapConfig test_config()
{
    goldsrc::NetchanBootstrapConfig config;
    config.first_packet_timeout = 100ms;
    config.fragment_completion_timeout = 40ms;
    config.maximum_datagrams_per_update = 8U;
    config.maximum_outgoing_packets_per_update = 1U;
    return config;
}

TEST_CASE("Netchan bootstrap sends one bounded project probe before polling",
          "[goldsrc][netchan][bootstrap]")
{
    CHECK(goldsrc::kDefaultNetchanFirstPacketTimeout == 5s);
    CHECK(goldsrc::kDefaultNetchanFragmentTimeout == 5s);
    CHECK(goldsrc::kMaximumNetchanBootstrapTimeout == 30s);
    CHECK(goldsrc::kDefaultNetchanBootstrapDatagramsPerUpdate == 8U);
    CHECK(goldsrc::kMaximumNetchanBootstrapDatagramsPerUpdate == 64U);
    CHECK(goldsrc::kDefaultNetchanBootstrapOutgoingPacketsPerUpdate == 1U);
    CHECK(goldsrc::kMaximumNetchanBootstrapOutgoingPacketsPerUpdate == 8U);

    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};

    REQUIRE(stage.start(epoch, transport.local));
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
    CHECK_FALSE(stage.terminal());
    CHECK(stage.remote_endpoint() == remote);
    REQUIRE(stage.local_endpoint());
    CHECK(*stage.local_endpoint() == transport.local);
    REQUIRE(stage.first_packet_deadline());
    CHECK(*stage.first_packet_deadline() == epoch + 100ms);
    CHECK_FALSE(stage.fragment_deadline());
    CHECK_FALSE(stage.result());
    CHECK_FALSE(stage.error());
    CHECK(transport.local_address_queries == 1U);
    CHECK(transport.receive_count == 0U);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.sent[0].destination == remote);
    CHECK(stage.transmitted_packet_count() == 1U);

    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent[0].payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.sequence == sequence(1U));
    CHECK_FALSE(decoded.packet->header.sequence.flags.reliable);
    CHECK_FALSE(decoded.packet->header.sequence.flags.fragmented);
    CHECK(decoded.packet->header.acknowledgement.sequence == sequence(0U));
    CHECK_FALSE(decoded.packet->header.acknowledgement.reliable);
    CHECK(decoded.packet->payload ==
          std::vector<std::byte>(8U, goldsrc::kStockProtocol48NetchanPaddingByte));
}

TEST_CASE("Netchan bootstrap can send a bounded synthetic initial reliable unit",
          "[goldsrc][netchan][bootstrap][reliable]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    auto config = test_config();
    const std::string marker{"RELIABLE_TEST_PAYLOAD"};
    const auto marker_bytes = std::as_bytes(std::span{marker.data(), marker.size()});
    config.initial_reliable_payload.assign(marker_bytes.begin(), marker_bytes.end());
    goldsrc::NetchanBootstrapStage stage{transport, remote, config};

    REQUIRE(stage.start({}, transport.local));
    REQUIRE(transport.sent.size() == 1U);
    const auto initial = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(initial.packet);
    CHECK(initial.packet->header.sequence.flags.reliable);
    CHECK(equal_bytes(initial.packet->payload, marker_bytes));
    CHECK(stage.channel().has_reliable_in_flight());

    transport.queue(
        remote,
        server_packet(1U, 1U, bytes({0x42U, 0x43U}), true, true));
    stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(equal_bytes(stage.result()->payload.bytes, bytes({0x42U, 0x43U})));
    CHECK_FALSE(stage.channel().has_reliable_payload());
}

TEST_CASE("Netchan bootstrap rejects invalid bounds endpoints and clock overflow",
          "[goldsrc][netchan][bootstrap][configuration]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto assert_invalid = [&](goldsrc::NetchanBootstrapConfig config,
                                    const network::NetworkAddress endpoint =
                                        network::NetworkAddress::loopback(27'128),
                                    const network::NetworkAddress expected_local =
                                        network::NetworkAddress::loopback(30'000),
                                    const goldsrc::NetchanBootstrapTimePoint now = {}) {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, endpoint, std::move(config)};
        CHECK_FALSE(stage.start(now, expected_local));
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::invalid_configuration);
        CHECK(transport.local_address_queries == 0U);
        CHECK(transport.send_attempt_count == 0U);
        CHECK(transport.receive_count == 0U);
    };

    auto config = test_config();
    config.first_packet_timeout = 0ms;
    assert_invalid(config);
    config = test_config();
    config.fragment_completion_timeout =
        goldsrc::kMaximumNetchanBootstrapTimeout + 1ms;
    assert_invalid(config);
    config = test_config();
    config.maximum_datagrams_per_update = 0U;
    assert_invalid(config);
    config = test_config();
    config.maximum_outgoing_packets_per_update =
        goldsrc::kMaximumNetchanBootstrapOutgoingPacketsPerUpdate + 1U;
    assert_invalid(config);
    config = test_config();
    config.maximum_datagram_size = goldsrc::kNetchanHeaderSize - 1U;
    assert_invalid(config);
    config = test_config();
    config.maximum_datagram_size = goldsrc::kMaximumNetchanDatagramSize + 1U;
    assert_invalid(config);
    config = test_config();
    config.reassembly_limits.maximum_reassembled_size = 0U;
    assert_invalid(config);
    config = test_config();
    config.reassembly_limits.maximum_fragment_count =
        goldsrc::kMaximumNormalFragmentsPerMessage + 1U;
    assert_invalid(config);
    config = test_config();
    config.channel_limits.maximum_packet_payload_size = 7U;
    assert_invalid(config);
    assert_invalid(test_config(), network::NetworkAddress{0U, 27'128});
    assert_invalid(test_config(), network::NetworkAddress::loopback(0U));
    assert_invalid(test_config(), remote, network::NetworkAddress::loopback(0U));
    assert_invalid(
        test_config(),
        remote,
        network::NetworkAddress::loopback(30'000),
        goldsrc::NetchanBootstrapTimePoint::max() - 1ms);
}

TEST_CASE("Netchan bootstrap requires same local endpoint through the stage lifetime",
          "[goldsrc][netchan][bootstrap][endpoint]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);

    SECTION("initial local query failure is typed and bounded")
    {
        FakeDatagramTransport transport;
        transport.local_error.assign(600U, 'x');
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        CHECK_FALSE(stage.start({}, transport.local));
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::local_endpoint_unavailable);
        CHECK(stage.error()->context.size() ==
              goldsrc::kNetchanBootstrapDiagnosticTextLimit);
        CHECK(transport.send_attempt_count == 0U);
    }

    SECTION("local endpoint drift is detected before receive")
    {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        transport.local = network::NetworkAddress::loopback(30'001);
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::local_endpoint_changed);
        CHECK(transport.receive_count == 0U);
        CHECK(transport.send_attempt_count == 1U);
    }
}

TEST_CASE("Netchan bootstrap send and receive failures are terminal",
          "[goldsrc][netchan][bootstrap][network]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);

    SECTION("initial send fails without committing outgoing sequence")
    {
        FakeDatagramTransport transport;
        transport.send_results.push_back(network::DatagramSendResult{
            network::DatagramSendStatus::error,
            "synthetic send failure",
        });
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        CHECK_FALSE(stage.start({}, transport.local));
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == goldsrc::NetchanBootstrapErrorCode::send_failed);
        CHECK(stage.channel().next_outgoing_sequence() == sequence(1U));
        CHECK(stage.transmitted_packet_count() == 0U);
        CHECK(transport.receive_count == 0U);
    }

    SECTION("receive error is typed")
    {
        FakeDatagramTransport transport;
        transport.incoming.push_back(network::DatagramTransportReceiveResult{
            network::DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic receive failure",
        });
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::receive_failed);
    }

    SECTION("ACK send failure withholds a ready payload")
    {
        FakeDatagramTransport transport;
        transport.send_results.push_back(network::DatagramSendResult{
            network::DatagramSendStatus::sent,
            {},
        });
        transport.send_results.push_back(network::DatagramSendResult{
            network::DatagramSendStatus::error,
            "synthetic ACK failure",
        });
        transport.queue(remote, server_packet(1U, 1U, bytes({0x31U}), true));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::network_error);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code == goldsrc::NetchanBootstrapErrorCode::send_failed);
        CHECK(stage.transmitted_packet_count() == 1U);
    }
}

TEST_CASE("Receive metadata is strict while wrong endpoint precedes truncation",
          "[goldsrc][netchan][bootstrap][transport]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto spoofed = network::NetworkAddress::loopback(27'129);

    SECTION("inconsistent source metadata is a network error")
    {
        FakeDatagramTransport transport;
        auto payload = server_packet(1U, 1U, bytes({0x41U}));
        const auto size = payload.size();
        transport.incoming.push_back(network::DatagramTransportReceiveResult{
            network::DatagramTransportReceiveStatus::received,
            network::Datagram{remote, std::move(payload)},
            spoofed,
            size,
            {},
        });
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::inconsistent_receive_result);
    }

    SECTION("spoofed truncation is ignored before a valid target packet")
    {
        FakeDatagramTransport transport;
        transport.queue_truncated(spoofed, 20'000U);
        transport.queue(remote, server_packet(1U, 1U, bytes({0x42U}), true));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
        CHECK(transport.receive_count == 2U);
        CHECK(stage.transmitted_packet_count() == 2U);
    }

    SECTION("target truncation is a protocol error")
    {
        FakeDatagramTransport transport;
        transport.queue_truncated(remote, 20'000U);
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::datagram_truncated);
    }
}

TEST_CASE("Every inconsistent datagram receive shape is rejected before parsing",
          "[goldsrc][netchan][bootstrap][transport][consistency]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto assert_inconsistent = [&](network::DatagramTransportReceiveResult result) {
        FakeDatagramTransport transport;
        transport.incoming.push_back(std::move(result));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::inconsistent_receive_result);
        CHECK(stage.channel().incoming_sequence() == sequence(0U));
        CHECK(stage.transmitted_packet_count() == 1U);
    };

    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::would_block,
        std::nullopt,
        remote,
        0U,
        {},
    });
    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::error,
        std::nullopt,
        remote,
        0U,
        "synthetic error with metadata",
    });
    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::received,
        network::Datagram{remote, bytes({0x01U})},
        std::nullopt,
        1U,
        {},
    });
    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::received,
        std::nullopt,
        remote,
        1U,
        {},
    });
    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::received,
        network::Datagram{remote, bytes({0x01U})},
        remote,
        2U,
        {},
    });
    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::received,
        network::Datagram{remote, bytes({0x01U})},
        remote,
        1U,
        "unexpected receive error text",
    });
    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::truncated,
        std::nullopt,
        std::nullopt,
        4'097U,
        "synthetic truncation without source",
    });
    assert_inconsistent(network::DatagramTransportReceiveResult{
        network::DatagramTransportReceiveStatus::truncated,
        network::Datagram{remote, bytes({0x01U})},
        remote,
        4'097U,
        "synthetic truncation with payload",
    });
}

TEST_CASE("Target connectionless special and malformed datagrams fail distinctly",
          "[goldsrc][netchan][bootstrap][classifier]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto assert_protocol_error = [&](std::vector<std::byte> datagram,
                                           const goldsrc::NetchanBootstrapErrorCode code) {
        FakeDatagramTransport transport;
        transport.queue(remote, std::move(datagram));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == code);
        CHECK(stage.transmitted_packet_count() == 1U);
    };

    assert_protocol_error(
        bytes({0xffU, 0xffU, 0xffU, 0xffU, 0x42U}),
        goldsrc::NetchanBootstrapErrorCode::unexpected_connectionless_packet);
    assert_protocol_error(
        bytes({0xfeU, 0xffU, 0xffU, 0xffU, 0U, 0U, 0U, 0U}),
        goldsrc::NetchanBootstrapErrorCode::unsupported_special_packet);
    assert_protocol_error(
        bytes({1U, 2U, 3U, 4U, 5U, 6U, 7U}),
        goldsrc::NetchanBootstrapErrorCode::malformed_packet);
}

TEST_CASE("Duplicate and older packets do not mutate channel state or cause ACK storms",
          "[goldsrc][netchan][bootstrap][sequence]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    transport.queue(remote, server_packet(0U, 0U, bytes({0x20U}), true));
    transport.queue(
        remote,
        server_packet(goldsrc::kNetchanSequenceMask, 0U, bytes({0x21U}), true));
    transport.queue(remote, server_packet(1U, 1U, bytes({0x22U}), true));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};

    REQUIRE(stage.start({}, transport.local));
    stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(equal_bytes(stage.result()->payload.bytes, bytes({0x22U})));
    CHECK(stage.channel().incoming_sequence() == sequence(1U));
    CHECK(stage.channel().incoming_reliable_toggle());
    CHECK(transport.receive_count == 3U);
    CHECK(stage.transmitted_packet_count() == 2U);
}

TEST_CASE("Half-range sequence and invalid acknowledgements fail before ACK",
          "[goldsrc][netchan][bootstrap][acknowledgement]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto assert_error = [&](std::vector<std::byte> datagram,
                                  const goldsrc::NetchanBootstrapErrorCode code) {
        FakeDatagramTransport transport;
        transport.queue(remote, std::move(datagram));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start({}, transport.local));
        stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == code);
        CHECK(stage.transmitted_packet_count() == 1U);
    };

    assert_error(
        server_packet(goldsrc::kNetchanSequenceHalfRange, 1U, bytes({0x31U})),
        goldsrc::NetchanBootstrapErrorCode::invalid_sequence);
    assert_error(
        server_packet(1U, 2U, bytes({0x32U})),
        goldsrc::NetchanBootstrapErrorCode::invalid_acknowledgement);
    assert_error(
        server_packet(1U, 1U, bytes({0x33U}), false, true),
        goldsrc::NetchanBootstrapErrorCode::invalid_acknowledgement);

    auto reserved_ack = server_packet(1U, 1U, bytes({0x34U}));
    reserved_ack[7U] |= std::byte{0x40U};
    assert_error(
        std::move(reserved_ack),
        goldsrc::NetchanBootstrapErrorCode::malformed_packet);
}

TEST_CASE("Padding is acknowledged but only non-padding becomes an owning result",
          "[goldsrc][netchan][bootstrap][payload][capture]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    transport.queue(
        remote,
        server_packet(
            1U,
            1U,
            std::vector<std::byte>(8U, goldsrc::kStockProtocol48NetchanPaddingByte)));
    transport.queue(remote, server_packet(2U, 2U, bytes({0x51U, 0x52U}), true));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};

    REQUIRE(stage.start({}, transport.local));
    stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
    CHECK_FALSE(stage.result());
    REQUIRE(transport.sent.size() == 2U);

    // Independent benign golden: seq2, ACK server seq1, no reliable ACK,
    // followed by the capture-confirmed key-2 encoding of eight 0x01 bytes.
    const auto expected_first_ack = bytes({
        0x02U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x00U,
        0x59U, 0x19U, 0x01U, 0x03U,
        0x19U, 0x01U, 0x11U, 0x43U,
    });
    CHECK(equal_bytes(transport.sent[1U].payload, expected_first_ack));

    stage.update(goldsrc::NetchanBootstrapTimePoint{} + 2ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(equal_bytes(stage.result()->payload.bytes, bytes({0x51U, 0x52U})));
    CHECK(stage.result()->payload.source_sequence == sequence(2U));
    CHECK(stage.result()->payload.reliable);
    CHECK_FALSE(stage.result()->payload.reassembled);
    CHECK(stage.result()->payload.fragment_count == 0U);
    CHECK(transport.sent.size() == 3U);
}

TEST_CASE("Five unique normal fragments toggle once each and complete in index order",
          "[goldsrc][netchan][bootstrap][fragments][capture]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    std::array<std::vector<std::byte>, 5U> fragments;
    std::vector<std::byte> expected;
    constexpr std::array<std::size_t, 5U> sizes{1'024U, 1'024U, 1'024U, 1'024U, 90U};
    for (std::size_t index = 0U; index < fragments.size(); ++index) {
        fragments[index].assign(
            sizes[index],
            std::byte{static_cast<std::uint8_t>(index + 1U)});
        expected.insert(expected.end(), fragments[index].begin(), fragments[index].end());
        transport.queue(
            remote,
            normal_fragment_packet(
                static_cast<std::uint32_t>(index + 1U),
                static_cast<std::uint32_t>(index + 1U),
                static_cast<std::uint16_t>(index + 1U),
                5U,
                fragments[index]));
    }
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};

    REQUIRE(stage.start(epoch, transport.local));
    for (std::size_t index = 0U; index < fragments.size(); ++index) {
        stage.update(epoch + std::chrono::milliseconds{static_cast<int>(index + 1U)});
        CHECK(stage.channel().incoming_sequence() ==
              sequence(static_cast<std::uint32_t>(index + 1U)));
        CHECK(stage.channel().incoming_reliable_toggle() == ((index % 2U) == 0U));
        if (index + 1U < fragments.size()) {
            CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_fragments);
            REQUIRE(stage.fragment_deadline());
            CHECK(*stage.fragment_deadline() == epoch + 1ms + 40ms);
        }
    }

    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(equal_bytes(stage.result()->payload.bytes, expected));
    CHECK(stage.result()->payload.reassembled);
    CHECK(stage.result()->payload.reliable);
    CHECK(stage.result()->payload.fragment_count == 5U);
    CHECK(stage.result()->payload.source_sequence == sequence(5U));
    CHECK_FALSE(stage.fragment_deadline());
    CHECK(stage.transmitted_packet_count() == 6U);

    // ACKs after the five unique reliable units carry alternating bit31 state.
    for (std::size_t index = 0U; index < 5U; ++index) {
        const auto ack = goldsrc::decode_client_to_server_netchan_packet(
            transport.sent[index + 1U].payload);
        REQUIRE(ack.packet);
        CHECK(ack.packet->header.acknowledgement.sequence ==
              sequence(static_cast<std::uint32_t>(index + 1U)));
        CHECK(ack.packet->header.acknowledgement.reliable == ((index % 2U) == 0U));
    }
}

TEST_CASE("Fresh-sequence exact fragment retransmission does not toggle or duplicate data",
          "[goldsrc][netchan][bootstrap][fragments][duplicate]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto first = bytes({0x61U, 0x62U});
    const auto second = bytes({0x63U});
    transport.queue(remote, normal_fragment_packet(1U, 1U, 1U, 2U, first));
    transport.queue(remote, normal_fragment_packet(2U, 2U, 1U, 2U, first));
    transport.queue(remote, normal_fragment_packet(3U, 3U, 2U, 2U, second));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};

    REQUIRE(stage.start(epoch, transport.local));
    stage.update(epoch + 1ms);
    CHECK(stage.channel().incoming_reliable_toggle());
    REQUIRE(stage.fragment_deadline());
    const auto fixed_deadline = *stage.fragment_deadline();

    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_fragments);
    CHECK(stage.channel().incoming_sequence() == sequence(2U));
    CHECK(stage.channel().incoming_reliable_toggle());
    REQUIRE(stage.fragment_deadline());
    CHECK(*stage.fragment_deadline() == fixed_deadline);

    stage.update(epoch + 3ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(equal_bytes(stage.result()->payload.bytes, bytes({0x61U, 0x62U, 0x63U})));
    CHECK_FALSE(stage.channel().incoming_reliable_toggle());
}

TEST_CASE("Same numeric fragment duplicate is ignored before reassembly and ACK",
          "[goldsrc][netchan][bootstrap][fragments][duplicate][sequence]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto first = bytes({0x64U, 0x65U});
    const auto second = bytes({0x66U});
    transport.queue(remote, normal_fragment_packet(1U, 1U, 1U, 2U, first));
    transport.queue(remote, normal_fragment_packet(1U, 1U, 1U, 2U, first));
    transport.queue(remote, normal_fragment_packet(2U, 2U, 2U, 2U, second));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};

    REQUIRE(stage.start(epoch, transport.local));
    stage.update(epoch + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_fragments);
    CHECK(stage.channel().incoming_sequence() == sequence(1U));
    CHECK(stage.channel().incoming_reliable_toggle());
    CHECK(stage.transmitted_packet_count() == 2U);
    REQUIRE(stage.fragment_deadline());
    const auto fixed_deadline = *stage.fragment_deadline();

    // The equal numeric sequence is discarded by the channel inspection token.
    // Polling then reaches seq2 in the same update; only seq2 creates an ACK.
    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(equal_bytes(stage.result()->payload.bytes, bytes({0x64U, 0x65U, 0x66U})));
    CHECK(stage.channel().incoming_sequence() == sequence(2U));
    CHECK_FALSE(stage.channel().incoming_reliable_toggle());
    CHECK(stage.transmitted_packet_count() == 3U);
    CHECK(transport.receive_count == 3U);
    CHECK_FALSE(stage.fragment_deadline());
    CHECK(fixed_deadline == epoch + 41ms);
}

TEST_CASE("Malformed duplicate and older bodies are ignored from the header peek",
          "[goldsrc][netchan][bootstrap][fragments][duplicate][malformed]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto first = bytes({0x67U, 0x68U});
    const auto second = bytes({0x69U});
    transport.queue(remote, normal_fragment_packet(1U, 1U, 1U, 2U, first));

    // Both datagrams have an invalid/reserved ACK word and an invalid one-byte
    // fragment body. A full codec pass would reject them. Numeric seq1 is an
    // exact duplicate and numeric seq0 is older after seq1 has been admitted.
    transport.queue(remote, bytes({
                                0x01U, 0x00U, 0x00U, 0xc0U,
                                0xffU, 0xffU, 0xffU, 0xffU,
                                0x02U,
                            }));
    transport.queue(remote, bytes({
                                0x00U, 0x00U, 0x00U, 0xc0U,
                                0xffU, 0xffU, 0xffU, 0xffU,
                                0x02U,
                            }));
    transport.queue(remote, normal_fragment_packet(2U, 2U, 2U, 2U, second));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};

    REQUIRE(stage.start(epoch, transport.local));
    stage.update(epoch + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_fragments);
    CHECK(stage.channel().incoming_sequence() == sequence(1U));
    CHECK(stage.channel().incoming_reliable_toggle());
    CHECK(stage.transmitted_packet_count() == 2U);
    REQUIRE(stage.fragment_deadline());
    const auto fixed_deadline = *stage.fragment_deadline();

    stage.update(epoch + 2ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    REQUIRE(stage.result());
    CHECK(equal_bytes(stage.result()->payload.bytes, bytes({0x67U, 0x68U, 0x69U})));
    CHECK(stage.channel().incoming_sequence() == sequence(2U));
    CHECK_FALSE(stage.channel().incoming_reliable_toggle());
    CHECK(stage.transmitted_packet_count() == 3U);
    CHECK(transport.receive_count == 4U);
    CHECK(fixed_deadline == epoch + 41ms);
    CHECK_FALSE(stage.fragment_deadline());
}

TEST_CASE("Strict slot0 bootstrap rejects decoded bytes outside descriptor length",
          "[goldsrc][netchan][bootstrap][fragments][layout]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    goldsrc::NetchanFragmentSlots slots;
    slots[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        0x0001'0001U,
        0U,
        1U,
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(1U),
                goldsrc::NetchanSequenceFlags{true, true},
            },
            goldsrc::NetchanAcknowledgementWord{sequence(1U), false},
        },
        std::move(slots),
        bytes({0x6aU, 0x6bU}),
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded.datagram);
    transport.queue(remote, std::move(*encoded.datagram));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};

    REQUIRE(stage.start({}, transport.local));
    stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::NetchanBootstrapErrorCode::invalid_fragment_layout);
    CHECK(stage.channel().incoming_sequence() == sequence(0U));
    CHECK_FALSE(stage.result());
    CHECK(stage.transmitted_packet_count() == 1U);
}

TEST_CASE("Secondary stream is terminal M3 boundary with zero persistence or ACK",
          "[goldsrc][netchan][bootstrap][secondary]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    transport.queue(remote, secondary_fragment_packet(1U, 1U));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};

    REQUIRE(stage.start({}, transport.local));
    stage.update(goldsrc::NetchanBootstrapTimePoint{} + 1ms);
    CHECK(stage.state() ==
          goldsrc::NetchanBootstrapState::secondary_stream_pending_m3);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::NetchanBootstrapErrorCode::secondary_stream_pending_m3);
    CHECK(stage.channel().incoming_sequence() == sequence(0U));
    CHECK(stage.transmitted_packet_count() == 1U);
}

TEST_CASE("First-packet and fixed fragment deadlines are deterministic",
          "[goldsrc][netchan][bootstrap][timeout]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};

    SECTION("first packet at exact deadline is late")
    {
        FakeDatagramTransport transport;
        transport.queue(remote, server_packet(1U, 1U, bytes({0x71U}), true));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 100ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::timed_out);
        CHECK(transport.receive_count == 0U);
        CHECK(transport.incoming.size() == 1U);
        CHECK(stage.transmitted_packet_count() == 1U);
    }

    SECTION("fragment deadline is fixed from first accepted fragment")
    {
        FakeDatagramTransport transport;
        transport.queue(remote, normal_fragment_packet(1U, 1U, 1U, 3U, bytes({0x71U})));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 10ms);
        REQUIRE(stage.fragment_deadline());
        CHECK(*stage.fragment_deadline() == epoch + 50ms);
        transport.queue(remote, normal_fragment_packet(2U, 2U, 2U, 3U, bytes({0x72U})));
        stage.update(epoch + 49ms);
        REQUIRE(stage.fragment_deadline());
        CHECK(*stage.fragment_deadline() == epoch + 50ms);
        stage.update(epoch + 50ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::timed_out);
        CHECK_FALSE(stage.fragment_deadline());
        CHECK_FALSE(stage.result());
    }

    SECTION("clock regression is a typed protocol error before receive")
    {
        FakeDatagramTransport transport;
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(epoch + 10ms, transport.local));
        stage.update(epoch + 9ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::NetchanBootstrapErrorCode::time_moved_backwards);
        CHECK(transport.receive_count == 0U);
    }
}

TEST_CASE("Polling and outgoing work are independently bounded per update",
          "[goldsrc][netchan][bootstrap][budget]")
{
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto spoofed = network::NetworkAddress::loopback(27'129);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};

    SECTION("receive budget")
    {
        FakeDatagramTransport transport;
        auto config = test_config();
        config.maximum_datagrams_per_update = 2U;
        transport.queue(spoofed, bytes({0x01U}));
        transport.queue(spoofed, bytes({0x02U}));
        transport.queue(remote, server_packet(1U, 1U, bytes({0x73U}), true));
        goldsrc::NetchanBootstrapStage stage{transport, remote, config};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
        CHECK(transport.receive_count == 2U);
        CHECK(transport.incoming.size() == 1U);
        stage.update(epoch + 2ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
        CHECK(transport.receive_count == 3U);
    }

    SECTION("one successful ACK stops polling at default TX budget")
    {
        FakeDatagramTransport transport;
        transport.queue(
            remote,
            server_packet(
                1U,
                1U,
                std::vector<std::byte>(8U, goldsrc::kStockProtocol48NetchanPaddingByte)));
        transport.queue(remote, server_packet(2U, 2U, bytes({0x74U}), true));
        goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(transport.receive_count == 1U);
        CHECK(transport.incoming.size() == 1U);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::waiting_first);
        stage.update(epoch + 2ms);
        CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
        CHECK(transport.receive_count == 2U);
    }
}

TEST_CASE("Cancellation completion and failures are terminal and send nothing later",
          "[goldsrc][netchan][bootstrap][terminal]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    transport.queue(remote, server_packet(1U, 1U, bytes({0x81U}), true));
    goldsrc::NetchanBootstrapStage stage{transport, remote, test_config()};
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};

    REQUIRE(stage.start(epoch, transport.local));
    stage.cancel(epoch + 1ms);
    stage.cancel(epoch + 2ms);
    stage.update(epoch + 3ms);
    CHECK_FALSE(stage.start(epoch + 4ms, transport.local));
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::cancelled);
    CHECK(stage.terminal());
    CHECK(transport.receive_count == 0U);
    CHECK(transport.incoming.size() == 1U);
    CHECK(transport.send_attempt_count == 1U);
}

TEST_CASE("Metadata trace callbacks cannot reenter and thrown callbacks are isolated",
          "[goldsrc][netchan][bootstrap][trace]")
{
    FakeDatagramTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'128);
    const auto epoch = goldsrc::NetchanBootstrapTimePoint{};
    transport.queue(remote, server_packet(1U, 1U, bytes({0x91U}), true));

    goldsrc::NetchanBootstrapStage* stage_pointer = nullptr;
    std::vector<goldsrc::NetchanBootstrapTraceEvent> events;
    bool reentrant_start = true;
    goldsrc::NetchanBootstrapStage stage{
        transport,
        remote,
        test_config(),
        [&](const goldsrc::NetchanBootstrapTraceEvent& event) {
            events.push_back(event);
            reentrant_start = stage_pointer->start(epoch, transport.local);
            stage_pointer->update(epoch + 1ms);
            stage_pointer->cancel(epoch + 1ms);
            throw std::runtime_error{"synthetic trace exception"};
        }};
    stage_pointer = &stage;

    bool started = false;
    REQUIRE_NOTHROW(started = stage.start(epoch, transport.local));
    REQUIRE(started);
    CHECK_FALSE(reentrant_start);
    REQUIRE_NOTHROW(stage.update(epoch + 1ms));
    CHECK(stage.state() == goldsrc::NetchanBootstrapState::complete);
    CHECK(events.size() >= 6U);
    CHECK(events.front().classification ==
          goldsrc::NetchanBootstrapTraceClassification::bootstrap_started);
    CHECK(events.back().classification ==
          goldsrc::NetchanBootstrapTraceClassification::bootstrap_complete);
    CHECK(events.back().endpoint == remote);
    CHECK(events.back().sequence == 1U);
    CHECK(events.back().acknowledgement == 1U);
    CHECK(events.back().reliable);
    CHECK_FALSE(events.back().fragmented);
    CHECK(events.back().payload_size == 1U);
    CHECK(events.back().transmitted_packet_count == 2U);
}

} // namespace
