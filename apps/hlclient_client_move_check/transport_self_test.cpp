#include "transport_self_test.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <stdexcept>
#include <thread>

namespace hlclient::tools {
namespace {
namespace g = goldsrc;
namespace n = network;
using namespace std::chrono_literals;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
g::NetchanSequence sequence(std::uint32_t n) {
    return *g::NetchanSequence::from_numeric(n);
}
// Existing transport seam, forwarding successful sends to the actual socket.
class ControlledTransport final : public n::IDatagramTransport {
  public:
    explicit ControlledTransport(n::UdpSocket socket) : udp(std::move(socket)) {}
    n::DatagramLocalAddressResult local_address() const override {
        return udp.local_address();
    }
    n::DatagramTransportReceiveResult receive(std::size_t size) override {
        return udp.receive(size);
    }
    n::DatagramSendResult send_to(const n::NetworkAddress &peer,
                                  std::span<const std::byte> bytes) override {
        if (block_once) {
            block_once = false;
            ++blocks;
            return {n::DatagramSendStatus::would_block, {}};
        }
        auto sent = udp.send_to(peer, bytes);
        if (sent)
            ++sends;
        return sent;
    }
    n::UdpDatagramTransport udp;
    bool block_once{};
    std::size_t sends{}, blocks{};
};
n::Datagram receive(n::UdpSocket &peer) {
    const auto deadline = g::NetchanDriverClock::now() + 2s;
    while (g::NetchanDriverClock::now() < deadline) {
        auto r = peer.receive();
        if (r.status == n::ReceiveStatus::received && r.datagram)
            return std::move(*r.datagram);
        require(r.status == n::ReceiveStatus::would_block, "peer_receive_failed");
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("peer_receive_timeout");
}
} // namespace

ReferenceMoveTransportSelfTestResult reference_move_transport_self_test() {
    ReferenceMoveTransportSelfTestResult result;
    try {
        n::NetworkRuntime runtime;
        require(runtime.valid(), "network_runtime_unavailable");
        std::string error;
        auto server = n::UdpSocket::open_ipv4(runtime, error);
        auto client = n::UdpSocket::open_ipv4(runtime, error);
        require(server.has_value() && client.has_value(), "sockets_unavailable");
        require(server->bind(n::NetworkAddress::loopback(0), error) &&
                    client->bind(n::NetworkAddress::loopback(0), error),
                "loopback_bind_failed");
        auto remote = server->local_address(error);
        auto local = client->local_address(error);
        require(remote.has_value() && local.has_value(), "local_addresses_unavailable");
        ControlledTransport transport{std::move(*client)};
        g::NetchanDriverConfig dc;
        dc.channel_inactivity_timeout = 5s;
        g::NetchanDriver driver{transport, *remote, dc};
        require(driver.start(g::NetchanDriverClock::now(), *local),
                "driver_start_failed");
        // Fresh owned test-session bootstrap; no authentication or captured
        // identity.
        g::ServerToClientNetchanPacket hello{
            {{sequence(1), {}}, {sequence(0), false}}, {}, {std::byte{1}}};
        auto hello_bytes = g::encode_server_to_client_netchan_packet(hello);
        require(bool(hello_bytes), "peer_bootstrap_encode_failed");
        require(server->send_to(*local, *hello_bytes.datagram, error),
                "peer_bootstrap_send_failed");
        const auto deadline = g::NetchanDriverClock::now() + 2s;
        while (!driver.session().first_acknowledgement_sent() &&
               g::NetchanDriverClock::now() < deadline) {
            driver.update(g::NetchanDriverClock::now());
            std::this_thread::sleep_for(1ms);
        }
        require(driver.session().first_acknowledgement_sent(), "bootstrap_timeout");
        (void)receive(*server); // Driver's bootstrap ACK, not a move.
        while (driver.poll_event()) {
        }
        const auto initial_sends = transport.sends;

        // Existing local descriptor fixture only, not a synthetic command
        // conversion. Capture mode continues to bind its actually captured registry
        // separately.
        auto fixture = g::make_synthetic_usercmd_schema_registry();
        require(bool(fixture), "fixture_schema_failed");
        auto binding = g::bind_goldsrc_usercmd_schema(
            *fixture.registry,
            g::GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1);
        require(bool(binding), "reference_binding_failed");
        g::GoldSrcUserCmdTransmissionConfig config;
        config.history.profile = g::GoldSrcUserCmdHistoryProfile::reference_wire_v1;
        config.history.generation = 73;
        config.planner.profile =
            g::GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1;
        config.planner.maximum_new_commands = 1;
        config.maximum_transmission_phases_per_update = 1;
        g::GoldSrcUserCmdTransmissionStage stage{
            driver,
            *binding.binding,
            {g::GoldSrcUserCmdSessionPrerequisiteProfile::
                 reference_loopback_test_ready_v1,
             true},
            config};
        require(stage.valid_configuration(), "reference_stage_invalid");
        const g::GoldSrcReferenceClientMoveCodec decoder{*binding.binding, 73};
        std::array<g::GoldSrcWireUserCmd, 6> commands{};
        commands[1].forward = 400;
        commands[1].msec = 10;
        commands[2].side = -400;
        commands[2].msec = 10;
        commands[3].angle_turns = {32768, 16384, 65535};
        commands[3].msec = 10;
        commands[4].buttons = 0xffff;
        commands[4].impulse = 7;
        commands[4].msec = 10;
        for (std::size_t i = 0; i < commands.size(); ++i) {
            require(bool(stage.queue_reference_command(
                        *g::GoldSrcUserCmdSequence::create(
                            static_cast<std::uint32_t>(i + 1)),
                        commands[i], 73)),
                    "history_insert_failed");
            ++result.queued;
        }
        for (std::size_t i = 0; i < commands.size(); ++i) {
            while (stage.poll_event()) {
            }
            const auto expected_sequence =
                driver.session().state().next_outgoing_sequence.value();
            require(bool(stage.update_reference(g::NetchanDriverClock::now())),
                    "prepare_failed");
            // Invalidate a prepared context with an unchanged-sequence reliable
            // prefix.
            if (i == 1) {
                constexpr std::array prefix{std::byte{3}, std::byte{'n'},
                                            std::byte{'e'}, std::byte{'w'},
                                            std::byte{0}};
                require(bool(driver.queue_reliable(prefix)), "reliable_queue_failed");
                require(bool(stage.update_reference(g::NetchanDriverClock::now())),
                        "stale_handling_failed");
                bool stale = false;
                while (auto e = stage.poll_event())
                    stale |= e->type ==
                             g::GoldSrcUserCmdTransmissionEventType::move_context_stale;
                require(stale, "stale_context_not_observed");
                ++result.stale_contexts;
                require(bool(stage.update_reference(g::NetchanDriverClock::now())),
                        "reprepare_failed");
            }
            if (i == 3) {
                transport.block_once = true;
                require(bool(stage.update_reference(g::NetchanDriverClock::now())),
                        "would_block_update_failed");
                require(stage.transmitted_packet_count() == i,
                        "would_block_counted_as_sent");
                require(stage.history()
                                .find(*g::GoldSrcUserCmdSequence::create(4))
                                ->new_transmission_count == 0,
                        "would_block_committed_history");
                ++result.retries;
            }
            require(bool(stage.update_reference(g::NetchanDriverClock::now())),
                    "send_failed");
            require(stage.transmitted_packet_count() == i + 1, "missing_send_receipt");
            result.packets_sent = transport.sends - initial_sends;
            auto received = receive(*server);
            ++result.packets_received;
            require(received.source == *local, "wrong_peer_source");
            require(received.payload.size() >= 8 && expected_sequence == i + 2,
                    "peer_literal_header_geometry");
            const std::array<std::byte, 8> expected_header{
                std::byte{static_cast<unsigned char>(i + 2)},
                std::byte{0},
                std::byte{0},
                i == 1 ? std::byte{0x80} : std::byte{0},
                std::byte{1},
                std::byte{0},
                std::byte{0},
                std::byte{0}};
            require(std::equal(expected_header.begin(), expected_header.end(),
                               received.payload.begin()),
                    "peer_literal_header_mismatch");
            // Intentional application-level drop after a real send/receive. No ACK.
            if (i == 2) {
                ++result.dropped;
                continue;
            }
            auto packet = g::decode_client_to_server_netchan_packet(received.payload);
            require(bool(packet), "peer_netchan_decode_failed");
            require(packet.packet->header.sequence.sequence.value() ==
                        expected_sequence,
                    "peer_sequence_mismatch");
            const auto prefix_size = i == 1 ? 5U : 0U;
            require(packet.packet->header.sequence.flags.reliable == (i == 1),
                    "peer_reliable_flag_mismatch");
            if (i == 1)
                require(packet.packet->payload.size() > 5 &&
                            packet.packet->payload[0] == std::byte{3} &&
                            packet.packet->payload[4] == std::byte{0},
                        "peer_prefix_mismatch");
            g::ReferenceMoveSource source;
            source.generation = 73;
            source.sequence = expected_sequence;
            auto move = decoder.decode(packet.packet->payload, prefix_size, source);
            require(bool(move), "peer_reference_decode_failed");
            ++result.checksum_matched;
            const auto walked = g::decode_reference_client_payload(
                decoder, packet.packet->payload, source);
            require(!walked.error && walked.moves.size() == 1 &&
                        walked.end_byte == packet.packet->payload.size(),
                    "peer_consumed_cursor_mismatch"); // Includes netchan's trailing
                                                      // clc_nop padding.
            const auto backups = std::min<std::size_t>(i, 2);
            require(move.message->backup_count == backups &&
                        move.message->new_count == 1,
                    "peer_command_counts_mismatch");
            for (std::size_t c = 0; c <= backups; ++c) {
                require(move.message->commands[c].value == commands[i - backups + c],
                        "peer_command_value_mismatch");
                ++result.expected_commands;
            }
            if (i == 0) {
                // Independently calculated zero command at outgoing sequence 2.
                constexpr std::array<std::byte, 7> literal{
                    std::byte{2},    std::byte{4},    std::byte{0xfc}, std::byte{2},
                    std::byte{0x19}, std::byte{0x50}, std::byte{2}};
                require(std::equal(literal.begin(), literal.end(),
                                   move.message->bytes.begin(),
                                   move.message->bytes.end()),
                        "peer_literal_mismatch");
            }
        }
        const auto final_history = stage.history();
        for (const auto &entry : final_history.entries()) {
            result.new_submitted += entry.new_transmission_count;
            result.backup_submitted += entry.backup_transmission_count;
        }
        result.history_revision = stage.history().revision();
        result.packets_sent = transport.sends - initial_sends;
        result.would_block = transport.blocks;
        stage.close(g::NetchanDriverClock::now());
        stage.close(g::NetchanDriverClock::now());
        result.cleanup = driver.cleanup_count() == 1;
        require(result.cleanup && result.packets_sent == 6 &&
                    result.new_submitted == 6 && result.backup_submitted == 9,
                "final_accounting_mismatch");
        result.passed = true;
    } catch (const std::exception &error) {
        result.failure = error.what();
    }
    return result; // All sockets destroyed on every exit path.
}
} // namespace hlclient::tools
