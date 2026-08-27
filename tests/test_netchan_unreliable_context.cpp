#include <hlclient/goldsrc/netchan_driver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

static_assert(!std::is_copy_constructible_v<goldsrc::NetchanOutgoingContextPlan>);
static_assert(std::is_nothrow_move_constructible_v<
              goldsrc::NetchanOutgoingContextPlan>);

class ContextTransport final : public network::IDatagramTransport {
public:
    struct SentDatagram {
        network::NetworkAddress destination;
        std::vector<std::byte> payload;
    };

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
        return network::DatagramSendResult{
            network::DatagramSendStatus::sent,
            {},
        };
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        const std::size_t maximum_size) override
    {
        ++receive_calls;
        receive_limits.push_back(maximum_size);
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

    std::optional<network::NetworkAddress> local{
        network::NetworkAddress::loopback(31'100U)};
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

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    std::vector<std::byte> output;
    output.reserve(text.size());
    std::ranges::transform(
        text,
        std::back_inserter(output),
        [](const char value) {
            return std::byte{static_cast<std::uint8_t>(value)};
        });
    return output;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    std::vector<std::byte> payload)
{
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{false, false},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                false,
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

[[nodiscard]] goldsrc::NetchanDriverConfig context_config()
{
    goldsrc::NetchanDriverConfig config;
    config.channel_inactivity_timeout = 1'000ms;
    config.fragment_transfer_timeout = 500ms;
    config.maximum_outgoing_packets_per_update = 1U;
    return config;
}

void require_runtime_ready(
    goldsrc::NetchanDriver& driver,
    ContextTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::NetchanDriverTimePoint epoch)
{
    REQUIRE(transport.local);
    REQUIRE(driver.start(epoch, *transport.local));
    transport.queue(remote, server_packet(1U, 0U, bytes("BOOTSTRAP")));
    driver.update(epoch + 1ms);
    REQUIRE(driver.state() == goldsrc::NetchanDriverState::active);
    REQUIRE(driver.session().first_acknowledgement_sent());
    REQUIRE(driver.session().state().next_outgoing_sequence == sequence(2U));
    REQUIRE(transport.sent.size() == 1U);
    transport.sent.clear();
}

void require_error(
    const goldsrc::NetchanDriverOperationResult& result,
    const goldsrc::NetchanDriverErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

} // namespace

TEST_CASE("Netchan outgoing context binds the exact next sequence and sends before RX",
          "[goldsrc][netchan][unreliable-context]")
{
    ContextTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, context_config()};
    require_runtime_ready(driver, transport, remote, epoch);

    auto prepared = driver.prepare_unreliable_context();
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    const auto plan_identity = prepared.plan->plan_identity();
    CHECK_FALSE(driver.last_sent_unreliable_context_identity());
    CHECK(prepared.plan->next_outgoing_sequence() == sequence(2U));
    CHECK(prepared.plan->plan_identity() != 0U);
    CHECK(prepared.plan->reliable_composition().decision ==
          goldsrc::ReliableTransmitDecision::none);
    CHECK(prepared.plan->reliable_composition().reliable_payload_size == 0U);
    CHECK_FALSE(prepared.plan->reliable_composition().reliable_sequence);
    CHECK_FALSE(prepared.plan->reliable_composition().fragmented_sequence);
    CHECK_FALSE(prepared.plan->reliable_composition().fragment);
    CHECK(prepared.plan->maximum_unreliable_payload_size() ==
          context_config().maximum_unreliable_payload_size);

    const auto payload = bytes("SEQUENCE_BOUND");
    REQUIRE(driver.commit_unreliable(std::move(*prepared.plan), payload));
    CHECK_FALSE(driver.last_sent_unreliable_context_identity());
    CHECK(driver.session().state().next_outgoing_sequence == sequence(2U));

    // If RX ran first this admitted packet would stale the retained plan.
    transport.queue(remote, server_packet(2U, 1U, bytes("LATER_ACK")));
    const auto receive_calls_before = transport.receive_calls;
    driver.update(epoch + 2ms);

    REQUIRE(transport.sent.size() == 1U);
    CHECK(transport.receive_calls == receive_calls_before);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.sequence == sequence(2U));
    REQUIRE(decoded.packet->payload.size() >= payload.size());
    CHECK(std::ranges::equal(
        payload,
        std::span<const std::byte>{decoded.packet->payload}.first(payload.size())));
    CHECK(driver.session().state().next_outgoing_sequence == sequence(3U));
    CHECK(driver.last_sent_unreliable_context_identity() == plan_identity);
}

TEST_CASE("Netchan outgoing context abandon is non-mutating and consumption is typed",
          "[goldsrc][netchan][unreliable-context]")
{
    ContextTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, context_config()};
    require_runtime_ready(driver, transport, remote, epoch);

    auto prepared = driver.prepare_unreliable_context();
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    const auto revision = prepared.plan->context_revision();
    REQUIRE(driver.abandon_unreliable(std::move(*prepared.plan)));
    CHECK(driver.session().state().next_outgoing_sequence == sequence(2U));

    const auto consumed = driver.commit_unreliable(
        std::move(*prepared.plan), bytes("NOT_SENT"));
    require_error(
        consumed,
        goldsrc::NetchanDriverErrorCode::stale_unreliable_context);

    auto replacement = driver.prepare_unreliable_context();
    REQUIRE(replacement);
    REQUIRE(replacement.plan);
    CHECK(replacement.plan->context_revision() == revision);
    CHECK(replacement.plan->plan_identity() != prepared.plan->plan_identity());
}

TEST_CASE("Foreign outgoing context remains valid for its owning driver",
          "[goldsrc][netchan][unreliable-context]")
{
    ContextTransport owner_transport;
    ContextTransport foreign_transport;
    foreign_transport.local = network::NetworkAddress::loopback(31'101U);
    const auto owner_remote = network::NetworkAddress::loopback(27'015U);
    const auto foreign_remote = network::NetworkAddress::loopback(27'016U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver owner{
        owner_transport, owner_remote, context_config()};
    goldsrc::NetchanDriver foreign{
        foreign_transport, foreign_remote, context_config()};
    require_runtime_ready(owner, owner_transport, owner_remote, epoch);
    require_runtime_ready(foreign, foreign_transport, foreign_remote, epoch);

    auto prepared = owner.prepare_unreliable_context();
    REQUIRE(prepared);
    REQUIRE(prepared.plan);
    const auto rejected = foreign.abandon_unreliable(
        std::move(*prepared.plan));
    require_error(
        rejected,
        goldsrc::NetchanDriverErrorCode::foreign_unreliable_context);

    REQUIRE(owner.commit_unreliable(
        std::move(*prepared.plan), bytes("OWNER_ONLY")));
    owner.update(epoch + 2ms);
    CHECK(owner_transport.sent.size() == 1U);
    CHECK(foreign_transport.sent.empty());
}

TEST_CASE("Reliable mutation stales context and regenerated plan composes reliable prefix",
          "[goldsrc][netchan][unreliable-context][reliable]")
{
    ContextTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, context_config()};
    require_runtime_ready(driver, transport, remote, epoch);

    auto stale = driver.prepare_unreliable_context();
    REQUIRE(stale);
    REQUIRE(stale.plan);
    const auto reliable = bytes("RELIABLE_PREFIX");
    REQUIRE(driver.queue_reliable(reliable));
    const auto rejected = driver.commit_unreliable(
        std::move(*stale.plan), bytes("STALE_SUFFIX"));
    require_error(
        rejected,
        goldsrc::NetchanDriverErrorCode::stale_unreliable_context);

    auto fresh = driver.prepare_unreliable_context();
    REQUIRE(fresh);
    REQUIRE(fresh.plan);
    CHECK(fresh.plan->next_outgoing_sequence() == sequence(2U));
    CHECK(fresh.plan->reliable_composition().decision ==
          goldsrc::ReliableTransmitDecision::send_new);
    CHECK(fresh.plan->reliable_composition().reliable_payload_size ==
          reliable.size());
    CHECK(fresh.plan->reliable_composition().reliable_sequence);

    const auto suffix = bytes("UNRELIABLE_SUFFIX");
    REQUIRE(driver.commit_unreliable(std::move(*fresh.plan), suffix));
    driver.update(epoch + 2ms);

    REQUIRE(transport.sent.size() == 1U);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.sequence == sequence(2U));
    CHECK(decoded.packet->header.sequence.flags.reliable);
    REQUIRE(decoded.packet->payload.size() >= reliable.size() + suffix.size());
    const auto body = std::span<const std::byte>{decoded.packet->payload};
    CHECK(std::ranges::equal(reliable, body.first(reliable.size())));
    CHECK(std::ranges::equal(
        suffix,
        body.subspan(reliable.size(), suffix.size())));
    REQUIRE(driver.session().in_flight_reliable_payload());
    CHECK(driver.session().in_flight_reliable_payload()->bytes == reliable);
}

TEST_CASE("Only one committed unreliable context is pending and sibling plans stale",
          "[goldsrc][netchan][unreliable-context][backpressure]")
{
    ContextTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, context_config()};
    require_runtime_ready(driver, transport, remote, epoch);

    auto first = driver.prepare_unreliable_context();
    auto sibling = driver.prepare_unreliable_context();
    REQUIRE(first);
    REQUIRE(first.plan);
    REQUIRE(sibling);
    REQUIRE(sibling.plan);
    REQUIRE(driver.commit_unreliable(
        std::move(*first.plan), bytes("FIRST_CONTEXT")));

    const auto unavailable = driver.prepare_unreliable_context();
    REQUIRE_FALSE(unavailable);
    REQUIRE(unavailable.error);
    CHECK(unavailable.error->code ==
          goldsrc::NetchanDriverErrorCode::unreliable_payload_pending);
    require_error(
        driver.submit_unreliable(bytes("LEGACY_SECOND")),
        goldsrc::NetchanDriverErrorCode::unreliable_payload_pending);
    require_error(
        driver.queue_reliable(bytes("RELIABLE_SECOND")),
        goldsrc::NetchanDriverErrorCode::unreliable_payload_pending);
    require_error(
        driver.commit_unreliable(
            std::move(*sibling.plan), bytes("SIBLING_CONTEXT")),
        goldsrc::NetchanDriverErrorCode::stale_unreliable_context);

    driver.update(epoch + 2ms);
    REQUIRE(transport.sent.size() == 1U);
    REQUIRE(driver.submit_unreliable(bytes("AFTER_SEND")));
}
