#include "../apps/hlclient_client_move_check/transport_self_test.hpp"
#include "goldsrc_usercmd_test_fixture.hpp"
#include "usercmd_transaction_test_access.hpp"
#include "../src/platform/sdl_platform_event_translator.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/games/halflife/movement_policy.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <SDL3/SDL_keycode.h>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <hlclient/goldsrc/reference_prediction_anchor.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

namespace {
namespace g = hlclient::goldsrc;
namespace n = hlclient::network;
namespace f = hlclient::test::usercmd_fixture;
using Access = g::detail::GoldSrcUserCmdTransactionalTestAccess;
using namespace std::chrono_literals;
class Transport final : public n::IDatagramTransport {
  public:
    n::DatagramLocalAddressResult local_address() const override {
        return {n::NetworkAddress::loopback(32100), {}};
    }
    n::DatagramSendResult send_to(const n::NetworkAddress &,
                                  std::span<const std::byte> bytes) override {
        ++attempts;
        if (block)
            return {n::DatagramSendStatus::would_block, {}};
        if (fail)
            return {n::DatagramSendStatus::error, "injected_failure"};
        sent.emplace_back(bytes.begin(), bytes.end());
        return {n::DatagramSendStatus::sent, {}};
    }
    n::DatagramTransportReceiveResult receive(std::size_t) override {
        if (incoming.empty())
            return {};
        auto bytes = std::move(incoming.front());
        incoming.pop_front();
        auto size = bytes.size();
        return {n::DatagramTransportReceiveStatus::received,
                n::Datagram{remote, std::move(bytes)},
                remote,
                size,
                {}};
    }
    n::NetworkAddress remote = n::NetworkAddress::loopback(32101);
    bool block{}, fail{};
    std::size_t attempts{};
    std::deque<std::vector<std::byte>> incoming;
    std::vector<std::vector<std::byte>> sent;
};
g::NetchanSequence seq(std::uint32_t i) { return *g::NetchanSequence::from_numeric(i); }
g::GoldSrcUserCmdSchemaBinding binding() {
    auto registry = f::exact_registry();
    auto bound = g::bind_goldsrc_usercmd_schema(
        registry,
        g::GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1);
    REQUIRE(bound);
    return std::move(*bound.binding);
}
g::GoldSrcUserCmdTransmissionConfig config(std::uint64_t generation = 17) {
    g::GoldSrcUserCmdTransmissionConfig c;
    c.history.profile = g::GoldSrcUserCmdHistoryProfile::reference_wire_v1;
    c.history.generation = generation;
    c.planner.profile = g::GoldSrcUserCmdPacketPlannerProfile::reference_backup_v1;
    return c;
}
constexpr g::GoldSrcUserCmdSessionPrerequisite ready{
    g::GoldSrcUserCmdSessionPrerequisiteProfile::reference_loopback_test_ready_v1,
    true};
void activate(g::NetchanDriver &driver, Transport &t) {
    REQUIRE(driver.start({}, *t.local_address().address));
    g::ServerToClientNetchanPacket packet{
        {{seq(1), {}}, {seq(0), false}}, {}, {std::byte{1}}};
    auto bytes = g::encode_server_to_client_netchan_packet(packet);
    REQUIRE(bytes);
    t.incoming.push_back(std::move(*bytes.datagram));
    driver.update(g::NetchanDriverTimePoint{} + 1ms);
    REQUIRE(driver.session().first_acknowledgement_sent());
    while (driver.poll_event()) {
    }
    t.sent.clear();
    t.attempts = 0;
}
g::ReferenceClientMoveMessage decode(const std::vector<std::byte> &bytes,
                                     std::uint64_t generation = 17,
                                     std::size_t prefix = 0) {
    const auto packet = g::decode_client_to_server_netchan_packet(bytes);
    REQUIRE(packet);
    g::ReferenceMoveSource source;
    source.generation = generation;
    source.sequence = packet.packet->header.sequence.sequence.value();
    auto move = g::GoldSrcReferenceClientMoveCodec{binding(), generation}.decode(
        packet.packet->payload, prefix, source);
    REQUIRE(move);
    const auto walked = g::decode_reference_client_payload(
        g::GoldSrcReferenceClientMoveCodec{binding(), generation},
        packet.packet->payload, source);
    CHECK_FALSE(walked.error);
    CHECK(walked.end_byte == packet.packet->payload.size());
    return std::move(*move.message);
}
} // namespace

TEST_CASE("Reference prediction carrier ledger binds only actual move sends to fresh direct clientdata",
          "[reference-transmission][prediction-anchor]") {
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    g::ReferencePredictionCarrierLedger ledger{4U};
    for (std::uint32_t id = 1U; id <= 3U; ++id) {
        g::GoldSrcWireUserCmd command;
        command.msec = 20U;
        command.forward = static_cast<std::int16_t>(id * 100U);
        REQUIRE(stage.queue_reference_command(f::sequence(id), command, 17U));
    }
    transport.block = true;
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    CHECK(transport.sent.empty());
    while (const auto event = stage.poll_event())
        CHECK(event->type != g::GoldSrcUserCmdTransmissionEventType::move_packet_submitted);
    CHECK(ledger.carriers().empty());

    transport.block = false;
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
    REQUIRE(transport.sent.size() == 1U);
    const auto first_wire = decode(transport.sent.back());
    CHECK(first_wire.new_count == 3U);
    CHECK(first_wire.backup_count == 0U);
    std::optional<g::GoldSrcUserCmdTransmissionEvent> first_receipt;
    while (auto event = stage.poll_event())
        if (event->type == g::GoldSrcUserCmdTransmissionEventType::move_packet_submitted)
            first_receipt = *event;
    REQUIRE(first_receipt);
    CHECK(ledger.record_sent(*first_receipt, stage.history()) ==
          g::ReferencePredictionAnchorStatus::bound);
    REQUIRE(ledger.carriers().size() == 1U);
    CHECK(ledger.carriers()[0].commands.size() == 3U);
    CHECK(ledger.carriers()[0].commands[2].value.forward == 300);
    CHECK(ledger.record_sent(*first_receipt, stage.history()) ==
          g::ReferencePredictionAnchorStatus::duplicate_or_old_receipt);

    g::GoldSrcWireUserCmd next;
    next.msec = 20U;
    next.forward = 120;
    REQUIRE(stage.queue_reference_command(f::sequence(4U), next, 17U));
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 4ms));
    REQUIRE(transport.sent.size() == 2U);
    const auto second_wire = decode(transport.sent.back());
    CHECK(second_wire.new_count == 1U);
    CHECK(second_wire.backup_count > 0U);
    std::optional<g::GoldSrcUserCmdTransmissionEvent> second_receipt;
    while (auto event = stage.poll_event())
        if (event->type == g::GoldSrcUserCmdTransmissionEventType::move_packet_submitted)
            second_receipt = *event;
    REQUIRE(second_receipt);
    CHECK(ledger.record_sent(*second_receipt, stage.history()) ==
          g::ReferencePredictionAnchorStatus::bound);
    CHECK(ledger.carriers().back().commands.back().identity.value() == 4U);
    CHECK_FALSE(ledger.carriers().back().commands.back().backup);

    auto source = g::ReferenceClientdataCarrier{17U, 100U, 40U,
        first_receipt->outgoing_netchan_sequence, true, false, false};
    auto anchor = ledger.bind_clientdata(source);
    REQUIRE(anchor.bound());
    CHECK(anchor.last_new_command->value() == 3U); // three new in one packet
    REQUIRE(anchor.last_new_value);
    CHECK(anchor.last_new_value->forward == 300);
    CHECK(ledger.bind_clientdata(source).status ==
          g::ReferencePredictionAnchorStatus::stale_or_duplicate_record);
    source.record_identity = 101U;
    source.source_sequence = 41U;
    source.acknowledgement = second_receipt->outgoing_netchan_sequence;
    source.source_reliable = true;
    CHECK(ledger.bind_clientdata(source).status ==
          g::ReferencePredictionAnchorStatus::old_body_or_reassembly);
    source.source_reliable = false;
    source.record_identity = 102U;
    source.source_sequence = 42U;
    source.acknowledgement = 99U;
    CHECK(ledger.bind_clientdata(source).status ==
          g::ReferencePredictionAnchorStatus::sent_carrier_missing);
    source.record_identity = 103U;
    source.source_sequence = 41U;
    CHECK(ledger.bind_clientdata(source).status ==
          g::ReferencePredictionAnchorStatus::stale_or_duplicate_record);
    source.record_identity = 104U;
    source.source_sequence = 43U;
    source.acknowledgement = second_receipt->outgoing_netchan_sequence;
    anchor = ledger.bind_clientdata(source);
    REQUIRE(anchor.bound());
    CHECK(anchor.last_new_command->value() == 4U); // backup IDs do not advance
    source.record_identity = 105U;
    source.source_sequence = 44U;
    source.acknowledgement = first_receipt->outgoing_netchan_sequence;
    const auto older_command = ledger.bind_clientdata(source);
    CHECK_FALSE(older_command.bound());
    CHECK(older_command.status != g::ReferencePredictionAnchorStatus::bound);
    CHECK(older_command.status == g::ReferencePredictionAnchorStatus::stale_command_boundary);
    source.record_identity = 106U;
    source.source_sequence = 45U;
    source.acknowledgement = second_receipt->outgoing_netchan_sequence;
    const auto same_command = ledger.bind_clientdata(source);
    REQUIRE(same_command.bound());
    CHECK(same_command.last_new_command->value() == 4U);
    source.record_identity = 107U; source.source_sequence = 46U;
    source.source_reliable = true; source.reassembled = true;
    CHECK(ledger.bind_clientdata(source).status == g::ReferencePredictionAnchorStatus::old_body_or_reassembly);
    source.record_identity = 108U; source.reassembled = false;
    source.contemporaneous_fragment_suffix = true;
    REQUIRE(ledger.bind_clientdata(source).bound()); // rejected completion did not reserve this same carrier
    source.record_identity = 109U; source.source_sequence = 47U;
    source.contemporaneous_fragment_suffix = false;
    CHECK(ledger.bind_clientdata(source).status == g::ReferencePredictionAnchorStatus::old_body_or_reassembly);
    source.record_identity = 110U; source.source_sequence = 48U;
    source.contemporaneous_fragment_suffix = true; source.reassembled = true;
    CHECK(ledger.bind_clientdata(source).status == g::ReferencePredictionAnchorStatus::old_body_or_reassembly);
}

namespace {
std::array<std::uint32_t, 3> coherent_frame_sent_carriers(
    g::ReferencePredictionCarrierLedger& ledger, const std::uint64_t generation = 17U) {
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config(generation)};
    std::array<std::uint32_t, 3> carriers{};
    for (std::uint32_t id = 1U; id <= carriers.size(); ++id) {
        g::GoldSrcWireUserCmd command;
        command.msec = 20U;
        command.forward = static_cast<std::int16_t>(id * 100U);
        REQUIRE(stage.queue_reference_command(f::sequence(id), command, generation));
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} +
            std::chrono::milliseconds{id + 1U}));
        std::optional<g::GoldSrcUserCmdTransmissionEvent> receipt;
        while (const auto event = stage.poll_event())
            if (event->type == g::GoldSrcUserCmdTransmissionEventType::move_packet_submitted)
                receipt = *event;
        REQUIRE(receipt);
        REQUIRE(receipt->outgoing_netchan_sequence);
        REQUIRE(ledger.record_sent(*receipt, stage.history()) ==
            g::ReferencePredictionAnchorStatus::bound);
        carriers[id - 1U] = *receipt->outgoing_netchan_sequence;
        REQUIRE(transport.sent.size() == id);
        CHECK(decode(transport.sent.back(), generation).commands.back().value == command);
    }
    return carriers;
}

hlclient::client::RuntimeClientObservationState coherent_movement_frame(
    const std::uint32_t acknowledgement, const std::uint64_t record = 100U,
    const std::uint32_t sequence = 40U, const double time = 10.0,
    const bool reliable = true, const bool suffix = false,
    const bool reassembled = false, const std::uint64_t generation = 17U) {
    namespace c = hlclient::client;
    c::RuntimeClientObservationState observation;
    observation.generation = generation;
    observation.publication_revision = record;
    observation.server_time_seconds = time;
    const auto metadata = [&](const std::size_t begin, const std::size_t end) {
        return c::RuntimeSubstateMetadata{generation,
            c::RuntimeObservationFreshness::observed_in_record,
            c::RuntimeObservationCompleteness::complete_reconstruction,
            c::RuntimeObservationSource{record, 1U, sequence, begin, end,
                acknowledgement, reliable, reassembled, suffix}};
    };
    observation.server_time_metadata = metadata(0U, 40U);
    observation.client_metadata = metadata(40U, 144U);
    observation.entity_metadata = metadata(144U, 336U);
    observation.receiving_client.emplace();
    c::RuntimePacketEntityObservation player;
    player.entity_number = 13U;
    player.player_movement_schema = true;
    observation.packet_entities.push_back(player);
    return observation;
}
} // namespace

TEST_CASE("Coherent reliable movement frames bind real carriers without weakening the plain anchor API",
          "[e322][prediction-anchor][reference-transmission][coherent-reliable]") {
    g::ReferencePredictionCarrierLedger ledger;
    const auto sent = coherent_frame_sent_carriers(ledger);
    auto initial = coherent_movement_frame(sent[0], 100U, 40U, 0.0);
    const auto strict = ledger.bind_clientdata({17U, 100U, 40U, sent[0], true, true, false});
    CHECK(strict.status == g::ReferencePredictionAnchorStatus::old_body_or_reassembly);
    CHECK_FALSE(strict.coherent_reliable_frame);
    CHECK(ledger.bind_observation(initial, 12U, false).status ==
        g::ReferencePredictionAnchorStatus::old_body_or_reassembly);
    const auto first = ledger.bind_observation(initial, 12U, true);
    REQUIRE(first.bound()); // An initial coherent frame need not wait for an unreliable one.
    CHECK(first.coherent_reliable_frame);
    CHECK(first.last_new_command->value() == 1U);
    CHECK(first.last_new_value->forward == 100);
    CHECK(first.outgoing_sequence == sent[0]);
    CHECK_FALSE(ledger.bind_observation(initial, 12U, true).bound());
    auto duplicate_sequence_ledger = ledger;
    CHECK(duplicate_sequence_ledger.bind_observation(
        coherent_movement_frame(sent[1], 101U, 40U, 0.01), 12U, true).status ==
        g::ReferencePredictionAnchorStatus::stale_or_duplicate_record);
    auto duplicate_record_ledger = ledger;
    CHECK(duplicate_record_ledger.bind_observation(
        coherent_movement_frame(sent[1], 100U, 41U, 0.01), 12U, true).status ==
        g::ReferencePredictionAnchorStatus::stale_or_duplicate_record);

    auto repeated_ack = coherent_movement_frame(sent[0], 101U, 41U, 0.02);
    const auto same_boundary = ledger.bind_observation(repeated_ack, 12U, true);
    REQUIRE(same_boundary.bound());
    CHECK(same_boundary.coherent_reliable_frame);
    CHECK(same_boundary.last_new_command->value() == 1U);
    auto second = coherent_movement_frame(sent[1], 102U, 42U, 0.04);
    REQUIRE(ledger.bind_observation(second, 12U, true).bound());
    auto lower_ack = coherent_movement_frame(sent[0], 103U, 43U, 0.06);
    const auto rejected = ledger.bind_observation(lower_ack, 12U, true);
    CHECK(rejected.status == g::ReferencePredictionAnchorStatus::stale_command_boundary);
    CHECK_FALSE(rejected.coherent_reliable_frame);
    // A rejected later-time frame must not poison the accepted time watermark.
    auto third = coherent_movement_frame(sent[2], 104U, 44U, 0.05);
    const auto advanced = ledger.bind_observation(third, 12U, true);
    REQUIRE(advanced.bound());
    CHECK(advanced.coherent_reliable_frame);
    CHECK(advanced.last_new_command->value() == 3U);
    CHECK(advanced.last_new_value->forward == 300);
    CHECK(initial.client_metadata.source->source_reliable);
    CHECK_FALSE(initial.client_metadata.source->contemporaneous_fragment_suffix);
    CHECK_FALSE(initial.client_metadata.source->reassembled);
}

TEST_CASE("Reliable movement admission requires every coherent ordered observation witness",
          "[e322][prediction-anchor][reference-transmission][coherent-reliable]") {
    namespace c = hlclient::client;
    g::ReferencePredictionCarrierLedger recorded;
    const auto sent = coherent_frame_sent_carriers(recorded);
    for (unsigned defect = 0U; defect != 35U; ++defect) {
        CAPTURE(defect);
        auto ledger = recorded;
        const auto valid = coherent_movement_frame(sent[0]);
        auto invalid = valid;
        auto& time = invalid.server_time_metadata;
        auto& client = invalid.client_metadata;
        auto& entities = invalid.entity_metadata;
        switch (defect) {
        case 0U: invalid.server_time_seconds.reset(); break;
        case 1U: invalid.server_time_seconds = std::numeric_limits<double>::quiet_NaN(); break;
        case 2U: invalid.server_time_seconds = std::numeric_limits<double>::infinity(); break;
        case 3U: invalid.server_time_seconds = -0.001; break;
        case 4U: invalid.receiving_client.reset(); break;
        case 5U: invalid.packet_entities.clear(); break;
        case 6U: invalid.packet_entities.front().entity_number = 12U; break;
        case 7U: invalid.packet_entities.front().player_movement_schema = false; break;
        case 8U: invalid.publication_revision = 0U; break;
        case 9U: ++entities.source->record_identity; break;
        case 10U: ++time.source->source_transport_sequence; break;
        case 11U: entities.source->carrier_acknowledgement = sent[1]; break;
        case 12U: time.source->source_reliable = false; break;
        case 13U: entities.source->reassembled = true; break;
        case 14U: time.source->contemporaneous_fragment_suffix = true; break;
        case 15U: ++entities.generation; break;
        case 16U: client.freshness = c::RuntimeObservationFreshness::retained; break;
        case 17U: time.freshness = c::RuntimeObservationFreshness::retained; break;
        case 18U: entities.completeness = c::RuntimeObservationCompleteness::unavailable; break;
        case 19U: time.source.reset(); break;
        case 20U: entities.source.reset(); break;
        case 21U: time.source->end_bit_offset = time.source->start_bit_offset; break;
        case 22U: client.source->end_bit_offset = client.source->start_bit_offset; break;
        case 23U: entities.source->end_bit_offset = entities.source->start_bit_offset; break;
        case 24U: time.source->end_bit_offset = client.source->start_bit_offset + 1U; break;
        case 25U: client.source->end_bit_offset = entities.source->start_bit_offset + 1U; break;
        case 26U: time.source->carrier_acknowledgement.reset(); break;
        case 27U: ++entities.source->record_ordinal; break;
        case 28U: client.source.reset(); break;
        case 29U: client.completeness = c::RuntimeObservationCompleteness::unavailable; break;
        case 30U: time.completeness = c::RuntimeObservationCompleteness::unavailable; break;
        case 31U: entities.freshness = c::RuntimeObservationFreshness::retained; break;
        case 32U: ++time.generation; break;
        case 33U: ++client.generation; break;
        case 34U: time.source->start_bit_offset = time.source->end_bit_offset + 1U; break;
        }
        const auto rejected = ledger.bind_observation(invalid, 12U, true);
        CHECK_FALSE(rejected.bound());
        CHECK_FALSE(rejected.coherent_reliable_frame);
        // A malformed witness cannot consume the same record/sequence or time.
        const auto recovered = ledger.bind_observation(valid, 12U, true);
        REQUIRE(recovered.bound());
        CHECK(recovered.coherent_reliable_frame);
    }
}

TEST_CASE("Accepted direct and fragment suffix frames advance the reliable movement time frontier",
          "[e322][prediction-anchor][reference-transmission][coherent-reliable]") {
    g::ReferencePredictionCarrierLedger ledger;
    const auto sent = coherent_frame_sent_carriers(ledger);
    const auto direct = ledger.bind_observation(
        coherent_movement_frame(sent[0], 100U, 40U, 10.0, false), 12U, true);
    REQUIRE(direct.bound());
    CHECK_FALSE(direct.coherent_reliable_frame);
    for (const auto stale_time : {9.0, 10.0}) {
        const auto stale = ledger.bind_observation(
            coherent_movement_frame(sent[1], 101U, 41U, stale_time), 12U, true);
        CHECK(stale.status == g::ReferencePredictionAnchorStatus::stale_movement_frame);
        CHECK_FALSE(stale.coherent_reliable_frame);
    }
    REQUIRE(ledger.bind_observation(
        coherent_movement_frame(sent[1], 101U, 41U, 10.02), 12U, true).bound());
    const auto suffix = ledger.bind_observation(
        coherent_movement_frame(sent[1], 102U, 42U, 10.5, true, true), 12U, true);
    REQUIRE(suffix.bound());
    CHECK_FALSE(suffix.coherent_reliable_frame);
    CHECK(ledger.bind_observation(
        coherent_movement_frame(sent[2], 103U, 43U, 10.49), 12U, true).status ==
        g::ReferencePredictionAnchorStatus::stale_movement_frame);
    REQUIRE(ledger.bind_observation(
        coherent_movement_frame(sent[2], 103U, 43U, 10.6), 12U, true).bound());
}

TEST_CASE("Coherent movement compatibility preserves legacy direct binding and rejects completed fragments",
          "[e322][prediction-anchor][reference-transmission][coherent-reliable]") {
    g::ReferencePredictionCarrierLedger recorded;
    const auto sent = coherent_frame_sent_carriers(recorded);
    SECTION("Missing movement witnesses do not tighten the original direct path or advance time") {
        auto ledger = recorded;
        REQUIRE(ledger.bind_observation(
            coherent_movement_frame(sent[0], 100U, 40U, 20.0, false), 12U, true).bound());
        auto legacy = coherent_movement_frame(sent[1], 101U, 41U, 100.0, false);
        legacy.entity_metadata.source.reset();
        const auto ordinary = ledger.bind_observation(legacy, 12U, true);
        REQUIRE(ordinary.bound());
        CHECK_FALSE(ordinary.coherent_reliable_frame);
        REQUIRE(ledger.bind_observation(
            coherent_movement_frame(sent[2], 102U, 42U, 21.0), 12U, true).bound());
    }
    SECTION("Rejected completion cannot reserve a following ordinary suffix in the same packet") {
        auto ledger = recorded;
        for (const bool suffix_flag : {false, true}) {
            const auto completed = ledger.bind_observation(
                coherent_movement_frame(sent[0], 100U, 40U, 100.0, true, suffix_flag, true),
                12U, true);
            CHECK(completed.status == g::ReferencePredictionAnchorStatus::old_body_or_reassembly);
            CHECK_FALSE(completed.coherent_reliable_frame);
        }
        const auto suffix = ledger.bind_observation(
            coherent_movement_frame(sent[0], 101U, 40U, 1.0, true, true), 12U, true);
        REQUIRE(suffix.bound());
        CHECK_FALSE(suffix.coherent_reliable_frame);
        REQUIRE(ledger.bind_observation(
            coherent_movement_frame(sent[1], 102U, 41U, 1.02), 12U, true).bound());
    }
    SECTION("A missing actual sent carrier cannot advance the time watermark") {
        auto ledger = recorded;
        CHECK(ledger.bind_observation(coherent_movement_frame(999U, 100U, 40U, 100.0),
            12U, true).status == g::ReferencePredictionAnchorStatus::sent_carrier_missing);
        REQUIRE(ledger.bind_observation(
            coherent_movement_frame(sent[0], 101U, 41U, 1.0), 12U, true).bound());
    }
}

TEST_CASE("Reliable movement chronology is local to the actual transmitted session generation",
          "[e322][prediction-anchor][reference-transmission][coherent-reliable]") {
    g::ReferencePredictionCarrierLedger old_ledger;
    const auto old_sent = coherent_frame_sent_carriers(old_ledger);
    REQUIRE(old_ledger.bind_observation(
        coherent_movement_frame(old_sent[0], 100U, 40U, 100.0), 12U, true).bound());
    g::ReferencePredictionCarrierLedger fresh_ledger;
    const auto fresh_sent = coherent_frame_sent_carriers(fresh_ledger, 18U);
    const auto fresh = coherent_movement_frame(fresh_sent[0], 1U, 1U, 0.0,
        true, false, false, 18U);
    CHECK(old_ledger.bind_observation(fresh, 12U, true).status ==
        g::ReferencePredictionAnchorStatus::generation_mismatch);
    const auto accepted = fresh_ledger.bind_observation(fresh, 12U, true);
    REQUIRE(accepted.bound());
    CHECK(accepted.coherent_reliable_frame);
    CHECK(accepted.generation == 18U);
    CHECK(accepted.source_record_identity == 1U);
    CHECK(accepted.last_new_command->value() == 1U);
}

TEST_CASE("D2 physical E through G1 policy immutable history and Netchan preserves tap hold release and backups",
          "[use][game-module][input][reference-transmission]") {
    namespace api = hlclient::game_api;
    namespace input = hlclient::input;
    namespace gi = hlclient::gameplay_input;
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({17U, 1U, 1U});
    const auto& policy = host.movement_policy();
    REQUIRE(api::valid_game_movement_policy(policy));
    CHECK(policy.bindings->actions_for_key(input::PhysicalKey::e) == gi::gameplay_input_action_mask(gi::GameplayInputAction::use));
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    input::InputStateTracker tracker;
    hlclient::platform::detail::SdlPlatformEventTranslator translator;
    g::LiveVisualButtonLatch latch;
    auto key = [](SDL_Scancode code, bool down, bool repeat = false) {
        SDL_Event native{};
        native.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        native.key.scancode = code;
        native.key.key = SDLK_Z; // layout must not change physical E
        native.key.repeat = repeat;
        return native;
    };
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    static_cast<void>(tracker.publish_snapshot());
    tracker.end_frame();
    auto frame = [&](std::initializer_list<SDL_Event> events) {
        tracker.begin_frame();
        for (const auto& native : events) {
            auto translated = translator.translate(native);
            REQUIRE(translated);
            const auto* event = std::get_if<input::InputEvent>(&*translated);
            REQUIRE(event);
            tracker.apply_event(*event);
        }
        auto intent = gi::GameplayInputIntentBuilder{}.build(tracker.publish_snapshot(), *policy.bindings, policy.mouse_look, 0.02);
        REQUIRE(intent);
        latch.observe(intent.intent->pressed_buttons() & policy.live_buttons, intent.intent->focused());
        tracker.end_frame();
        return std::move(*intent.intent);
    };
    auto sample = [&](const gi::GameplayInputIntent& intent, unsigned id, std::uint16_t buttons, int forward = 0) {
        g::GoldSrcUserCmdBuildContext context;
        context.command_sequence = f::sequence(id);
        context.command_msec = 20U;
        context.command_sample_duration_nanoseconds = 20'000'000U;
        context.movement_speeds = policy.movement_speeds;
        context.reference_movement.speed_key_multiplier = policy.speed_key_multiplier;
        context.reference_button_policy = policy.button_policy;
        context.one_shot_buttons = latch.pending();
        const auto camera = hlclient::gameplay_camera::GameplayCameraState::create({});
        REQUIRE(camera);
        auto wire = g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(intent, *camera.state, context);
        REQUIRE(wire);
        CHECK(wire.command->buttons == buttons);
        CHECK(wire.command->forward == forward);
        REQUIRE(stage.queue_reference_command(f::sequence(id), *wire.command, 17U));
        if (wire.one_shot_plan) {
            REQUIRE(wire.one_shot_plan->commit_after_history_insert(f::sequence(id)));
            latch.consume_after_history_insert(wire.one_shot_plan->consumes_buttons());
        }
        CHECK(*stage.history().find(f::sequence(id))->reference_command == *wire.command);
        return *wire.command;
    };
    auto tap = frame({key(SDL_SCANCODE_E, true), key(SDL_SCANCODE_E, false)});
    CHECK(tap.held_buttons() == 0U);
    CHECK(latch.pending() == gi::gameplay_button_mask(gi::GameplayButton::use));
    const auto first = sample(frame({}), 1U, 32U);
    CHECK(latch.pending() == 0U);
    transport.block = true;
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
    CHECK(transport.sent.empty());
    CHECK(*stage.history().find(f::sequence(1U))->reference_command == first);
    transport.block = false;
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 4ms));
    REQUIRE(transport.sent.size() == 1U);
    auto packet = g::decode_client_to_server_netchan_packet(transport.sent.back());
    REQUIRE(packet);
    auto plaintext = packet.packet->payload;
    REQUIRE(g::transform_reference_move_body(std::span<std::byte>{plaintext}.subspan(3U), std::to_integer<unsigned char>(plaintext[1U]), packet.packet->header.sequence.sequence.value(), true));
    // Independent LSB packing: 24-bit counts; 3-bit mask length 1,
    // mask 0x12 (msec/buttons), msec 20, uint16 buttons 32, zero padding.
    const std::vector<std::byte> expected{std::byte{0},std::byte{0},std::byte{1},std::byte{0x91},std::byte{0xa0},std::byte{0},std::byte{1},std::byte{0}};
    CHECK(std::vector<std::byte>(plaintext.begin()+3, plaintext.end()) == expected);
    CHECK(decode(transport.sent.back()).commands.back().value.buttons == 32U);
    sample(frame({}), 2U, 0U);
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 5ms));
    const auto released = decode(transport.sent.back());
    CHECK(released.commands.back().value.buttons == 0U);
    REQUIRE(released.backup_count > 0U);
    CHECK(released.commands.front().value.buttons == 32U);
    sample(frame({key(SDL_SCANCODE_E,true),key(SDL_SCANCODE_W,true)}),3U,32U|8U,400);
    sample(frame({key(SDL_SCANCODE_E,true,true)}),4U,32U|8U,400);
    CHECK(latch.pending() == 0U);
    sample(frame({key(SDL_SCANCODE_LSHIFT,true)}),5U,32U|8U,120);
    sample(frame({key(SDL_SCANCODE_SPACE,true),key(SDL_SCANCODE_LCTRL,true),key(SDL_SCANCODE_R,true)}),6U,32U|8U|2U|4U|8192U,120);
    sample(frame({key(SDL_SCANCODE_E,false)}),7U,8U|2U|4U|8192U,120);
    SDL_Event attack{}; attack.type=SDL_EVENT_MOUSE_BUTTON_DOWN; attack.button.button=SDL_BUTTON_LEFT;
    sample(frame({key(SDL_SCANCODE_E,true),attack}),8U,32U|8U|2U|4U|8192U|1U,120);
    CHECK(*stage.history().find(f::sequence(1U))->reference_command == first);
    host.teardown();
    CHECK_FALSE(host.active());
}

TEST_CASE("G1 aimed Use retains the server-facing pitch through Netchan and backups",
          "[pitch-boundary][use][game-module][reference-transmission]") {
    namespace gi = hlclient::gameplay_input;
    namespace input = hlclient::input;
    hlclient::game_api::GameClientHost host{
        hlclient::games::halflife::make_half_life_client_module()};
    host.reset({17U, 1U, 1U});
    const auto& policy = host.movement_policy();
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    input::InputStateTracker tracker;
    for (unsigned id = 1; id <= 2; ++id) {
        tracker.begin_frame();
        if (id == 1) {
            tracker.apply_event(input::InputEvent::focus_gained());
            tracker.apply_event(input::InputEvent::capture_acquired());
            tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::e));
        } else {
            tracker.apply_event(input::InputEvent::key_released(input::PhysicalKey::e));
        }
        const auto intent = gi::GameplayInputIntentBuilder{}.build(
            tracker.publish_snapshot(), *policy.bindings, policy.mouse_look, 0.02);
        REQUIRE(intent);
        hlclient::gameplay_camera::GameplayCameraStateCreateInfo camera_info;
        camera_info.yaw_degrees = 90.0;
        camera_info.pitch_degrees = id == 1 ? -45.0 : 45.0;
        const auto camera = hlclient::gameplay_camera::GameplayCameraState::create(camera_info);
        REQUIRE(camera);
        g::GoldSrcUserCmdBuildContext context;
        context.command_sequence = f::sequence(id);
        context.command_msec = 20U;
        context.command_sample_duration_nanoseconds = 20'000'000U;
        context.reference_button_policy = policy.button_policy;
        context.movement_speeds = policy.movement_speeds;
        const auto command = g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(
            *intent.intent, *camera.state, context);
        REQUIRE(command);
        REQUIRE(stage.queue_reference_command(f::sequence(id), *command.command, 17U));
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + std::chrono::milliseconds{2 * id}));
        const auto sent = decode(transport.sent.back());
        const auto& current = sent.commands.back().value;
        CHECK(current.angle_turns[0] == (id == 1 ? 8192U : 57344U));
        CHECK(current.angle_turns[1] == 16384U);
        CHECK(current.buttons == (id == 1 ? 32U : 0U));
        CHECK(current == *stage.history().find(f::sequence(id))->reference_command);
        if (id == 1) {
            // Project-owned button below the eye, nearest point (0,16,-8).
            // Independent SDK-facing cone predicate; no proprietary BSP needed.
            constexpr double radians = 3.14159265358979323846 / 180.0;
            const double pitch = current.angle_turns[0] * 360.0 / 65536.0 * radians;
            const double dot = (16.0 * std::cos(pitch) + 8.0 * std::sin(pitch)) / std::sqrt(320.0);
            CHECK(dot > 0.7);
            const auto replay = g::reference_jump_duck_weapon_use_movement_command(f::sequence(id), current);
            REQUIRE(replay);
            CHECK(replay.state->view_angles()[0] == 45.0F); // already wire-native: no second inversion
            CHECK(transport.sent.size() == 1U);
        } else {
            REQUIRE(sent.backup_count > 0U);
            CHECK(sent.commands.front().value.angle_turns[0] == 8192U);
            CHECK(sent.commands.front().value.buttons == 32U);
        }
        tracker.end_frame();
    }
    host.teardown();
}

TEST_CASE("D2 capture and life scoped input gate never resumes stale Use",
          "[use][game-module][input]") {
    namespace gi = hlclient::gameplay_input;
    hlclient::game_api::ScopedGameplayButtonGate gate;
    const auto use = gi::gameplay_button_mask(gi::GameplayButton::use);
    const auto jump = gi::gameplay_button_mask(gi::GameplayButton::jump);
    auto held = use | jump, pressed = use;
    gate.filter(use, false, held, pressed, 0U);
    CHECK(held == jump); CHECK(pressed == 0U);
    held = use | jump;
    gate.filter(use, true, held, pressed, 0U);
    CHECK(held == jump);
    gate.filter(use, true, held, pressed, use); // physical release
    held = use; pressed = use;
    gate.filter(use, true, held, pressed, 0U);
    CHECK(held == use); CHECK(pressed == use);
    gate.invalidate(use); // death or new life: same production gate
    held = use; pressed = 0U;
    gate.filter(use, true, held, pressed, 0U);
    CHECK(held == 0U);
    held = use; pressed = use; // a genuinely new physical press
    gate.filter(use, true, held, pressed, 0U);
    CHECK(held == use);
}

TEST_CASE("E16 secondary passes normal policy Netchan wire and immutable backups",
          "[e16][secondary][reference-transmission][game-module]") {
    namespace gi = hlclient::gameplay_input;
    namespace input = hlclient::input;
    const auto policy = hlclient::games::halflife::make_movement_policy();
    REQUIRE(policy.button_policy == g::GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use);
    const auto secondary = gi::gameplay_button_mask(gi::GameplayButton::attack_secondary);
    CHECK((policy.live_buttons & secondary) != 0U);
    CHECK((policy.capture_life_scoped_buttons & secondary) != 0U);
    Transport transport; g::NetchanDriver driver{transport, transport.remote}; activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    input::InputStateTracker tracker;
    for (unsigned id = 1; id <= 3; ++id) {
        tracker.begin_frame();
        if (id == 1) {
            tracker.apply_event(input::InputEvent::focus_gained());
            tracker.apply_event(input::InputEvent::capture_acquired());
            tracker.apply_event(input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::right));
        } else if (id == 3)
            tracker.apply_event(input::InputEvent::mouse_button_released(input::PhysicalMouseButton::right));
        const auto intent = gi::GameplayInputIntentBuilder{}.build(
            tracker.publish_snapshot(), *policy.bindings, policy.mouse_look, .02);
        REQUIRE(intent);
        const auto camera = hlclient::gameplay_camera::GameplayCameraState::create({}); REQUIRE(camera);
        g::GoldSrcUserCmdBuildContext context; context.command_sequence = f::sequence(id);
        context.command_msec = 20; context.command_sample_duration_nanoseconds = 20'000'000U;
        context.reference_button_policy = policy.button_policy;
        const auto wire = g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(*intent.intent, *camera.state, context);
        REQUIRE(wire); CHECK(wire.command->buttons == (id <= 2 ? 2048U : 0U));
        REQUIRE(stage.queue_reference_command(f::sequence(id), *wire.command, 17U));
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + std::chrono::milliseconds{2 * id}));
        const auto sent = decode(transport.sent.back());
        CHECK(sent.commands.back().value == *wire.command);
        CHECK(*stage.history().find(f::sequence(id))->reference_command == *wire.command);
        if (id > 1) {
            REQUIRE(sent.backup_count > 0);
            CHECK(sent.commands.front().value.buttons == 2048U);
        }
        tracker.end_frame();
    }
    hlclient::game_api::ScopedGameplayButtonGate gate;
    auto held = secondary, pressed = secondary;
    gate.filter(secondary, false, held, pressed, 0U); CHECK(held == 0U); CHECK(pressed == 0U);
    held = secondary; gate.filter(secondary, true, held, pressed, 0U); CHECK(held == 0U);
    gate.filter(secondary, true, held, pressed, secondary); // release required before another life/capture.
    held = secondary; pressed = secondary;
    gate.filter(secondary, true, held, pressed, 0U); CHECK(held == secondary);
    gate.invalidate(secondary); held = secondary; pressed = 0U;
    gate.filter(secondary, true, held, pressed, 0U); CHECK(held == 0U);
}

TEST_CASE("Half Life selects the server-owned longjump policy explicitly",
    "[longjump][game-module][movement]")
{
    const auto policy = hlclient::games::halflife::make_movement_policy();
    REQUIRE(hlclient::game_api::valid_game_movement_policy(policy));
    CHECK(policy.crouch_jump_physics_key == "slj");
    REQUIRE(policy.movement_config.crouch_jump);
    CHECK(policy.movement_config.crouch_jump->forward_speed == 560.0F);
    CHECK(policy.movement_config.crouch_jump->vertical_impulse == std::sqrt(89600.0F));
    CHECK(policy.movement_config.crouch_jump->minimum_speed == 50.0F);
    auto malformed = policy;
    malformed.crouch_jump_physics_key = "\\slj";
    CHECK_FALSE(hlclient::game_api::valid_game_movement_policy(malformed));
    malformed = policy; malformed.crouch_jump_physics_key.clear();
    CHECK_FALSE(hlclient::game_api::valid_game_movement_policy(malformed));
    malformed.movement_config.crouch_jump.reset();
    CHECK(hlclient::game_api::valid_game_movement_policy(malformed));
    malformed = policy; malformed.movement_config.crouch_jump->forward_speed =
        std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(hlclient::game_api::valid_game_movement_policy(malformed));
}

TEST_CASE("Reference seed requires coherent player data and exact button provenance",
          "[prediction-seed]") {
    namespace c = hlclient::client;
    c::RuntimeClientObservationState observed;
    observed.generation = 17U;
    const c::RuntimeObservationSource source{100U, 1U, 40U, 0U, 8U,
        3U, false, false};
    observed.client_metadata = {17U,
        c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction, source};
    observed.entity_metadata = observed.client_metadata;
    c::RuntimeReceivingClientObservation client;
    client.origin = {10.0, 20.0, 30.0};
    client.velocity = {100.0, 0.0, 0.0};
    client.view_offset = {0.0, 0.0, 28.0};
    client.flags = 1U << 9U;
    client.water_level = 0U;
    client.dead_flag = 0U;
    client.in_duck = false;
    client.maximum_speed = 270.0;
    observed.receiving_client = client;
    c::RuntimePacketEntityObservation player;
    player.entity_number = 2U; // ServerInfo slot 1 -> entity 2.
    player.player_movement_schema = true;
    player.move_type = 3U;
    player.use_hull = 0U;
    player.gravity_multiplier = 1.0;
    player.friction_multiplier = 1.0;
    player.base_velocity = {0.0, 0.0, 0.0};
    player.spectator = false;
    observed.packet_entities.push_back(player);
    g::ReferencePredictionAnchorResult anchor;
    anchor.status = g::ReferencePredictionAnchorStatus::bound;
    anchor.last_new_command = f::sequence(3U);
    anchor.last_new_value = g::GoldSrcWireUserCmd{};
    anchor.outgoing_sequence = 3U;
    anchor.generation = 17U;
    anchor.source_record_identity = 100U;
    anchor.source_sequence = 40U;
    auto seed = g::inspect_reference_prediction_seed(observed, 1U, anchor);
    REQUIRE(seed.seed);
    CHECK(seed.status == g::ReferencePredictionSeedStatus::ready);
    CHECK(seed.seed->receiving_entity_number == 2U);
    CHECK(seed.seed->old_buttons_origin ==
          g::ReferencePredictionFieldOrigin::reference_anchored_neutral_command);
    CHECK(seed.seed->origin.x == 10.0);
    CHECK(seed.seed->maximum_speed == 270.0);
    // The game-selected exact capability is server-owned; reset/missing/key
    // lookalikes and malformed tails cannot retain a prior enabled state.
    for (const auto& [text, expected] : std::array{
            std::pair{"\\slj\\1", true}, std::pair{"other\\v\\slj\\1", true},
            std::pair{"\\slj\\0", false}, std::pair{"\\notslj\\1", false},
            std::pair{"\\slj\\1\\broken", false}, std::pair{"\\slj\\11", false},
            std::pair{"\\slj\\0\\slj\\1", false}, std::pair{"", false}}) {
        observed.receiving_client->physics_info = text;
        const auto ability = g::inspect_reference_prediction_seed(observed, 1U, anchor, {}, "slj");
        REQUIRE(ability.seed); CHECK(ability.seed->crouch_jump_enabled == expected);
        const auto unselected = g::inspect_reference_prediction_seed(observed, 1U, anchor);
        REQUIRE(unselected.seed); CHECK_FALSE(unselected.seed->crouch_jump_enabled);
    }
    observed.receiving_client->physics_info.reset();
    const auto absent_ability = g::inspect_reference_prediction_seed(observed, 1U, anchor, {}, "slj");
    REQUIRE(absent_ability.seed); CHECK_FALSE(absent_ability.seed->crouch_jump_enabled);
    observed.receiving_client->view_offset =
        {std::nullopt, std::nullopt, 28.0};
    const auto vertical_only =
        g::inspect_reference_prediction_seed(observed, 1U, anchor);
    REQUIRE(vertical_only.seed);
    CHECK(vertical_only.seed->view_offset.x == 0.0);
    CHECK(vertical_only.seed->view_offset.y == 0.0);
    CHECK(vertical_only.seed->view_offset_origin ==
          g::ReferencePredictionFieldOrigin::
              reference_vertical_only_view_offset_policy);
    observed.receiving_client->view_offset = {0.0, 0.0, 28.0};

    anchor.source_record_identity = 101U;
    CHECK(g::inspect_reference_prediction_seed(observed, 1U, anchor).status ==
          g::ReferencePredictionSeedStatus::incoherent_records);
    anchor.source_record_identity = 100U;

    observed.entity_metadata.source->record_identity = 99U;
    CHECK(g::inspect_reference_prediction_seed(observed, 1U, anchor).status ==
          g::ReferencePredictionSeedStatus::incoherent_records);
    observed.entity_metadata.source = source;
    observed.receiving_client->flags.reset();
    const auto missing = g::inspect_reference_prediction_seed(observed, 1U, anchor);
    CHECK(missing.status == g::ReferencePredictionSeedStatus::missing_semantic_field);
    CHECK(missing.field == g::ReferencePredictionSeedField::flags);
    observed.receiving_client->flags = 1U << 9U;
    CHECK(g::inspect_reference_prediction_seed(observed, 0U, anchor).status ==
          g::ReferencePredictionSeedStatus::player_entity_unavailable);
    observed.receiving_client->water_level = 1U;
    const auto liquid = g::inspect_reference_prediction_seed(observed, 1U, anchor);
    CHECK(liquid.status == g::ReferencePredictionSeedStatus::unsupported_context);
    CHECK(liquid.field == g::ReferencePredictionSeedField::water_level);
    observed.receiving_client->water_level = 0U;
    // Completed FL_DUCKING/usehull=1 is not Valve's transient bInDuck.
    observed.receiving_client->flags = (1U << 9U) | (1U << 14U);
    observed.receiving_client->in_duck = false;
    observed.receiving_client->duck_time = 580U;
    observed.packet_entities[0].use_hull = 1U;
    const auto crouched = g::inspect_reference_prediction_seed(
        observed, 1U, anchor);
    REQUIRE(crouched.seed);
    CHECK(crouched.seed->use_hull == 1U);
    CHECK_FALSE(crouched.seed->in_duck);
    CHECK(crouched.seed->duck_time_milliseconds == 580U);
    observed.receiving_client->flags = 1U << 9U;
    observed.receiving_client->duck_time.reset();
    observed.packet_entities[0].use_hull = 0U;
    anchor.last_new_value->buttons =
        g::kReferenceGoldSrcButtonAttack | g::kReferenceGoldSrcButtonReload;
    const auto weapon_only = g::inspect_reference_prediction_seed(
        observed, 1U, anchor);
    REQUIRE(weapon_only.seed);
    CHECK(weapon_only.seed->old_buttons == 0U);
    CHECK(weapon_only.seed->old_buttons_origin ==
          g::ReferencePredictionFieldOrigin::reference_anchored_weapon_only_policy);
    anchor.last_new_value->buttons = g::kReferenceGoldSrcButtonJump;
    CHECK(g::inspect_reference_prediction_seed(observed, 1U, anchor).status ==
          g::ReferencePredictionSeedStatus::matching_prediction_slot_required);
    seed = g::inspect_reference_prediction_seed(observed, 1U, anchor,
        g::ReferenceRetainedPredictionButtons{f::sequence(3U),
            g::kReferenceGoldSrcButtonJump});
    REQUIRE(seed.seed);
    CHECK(seed.seed->old_buttons == g::kReferenceGoldSrcButtonJump);
    CHECK(seed.seed->old_buttons_origin ==
          g::ReferencePredictionFieldOrigin::exact_retained_prediction_slot);
    CHECK(g::inspect_reference_prediction_seed(observed, 1U, anchor,
        g::ReferenceRetainedPredictionButtons{f::sequence(2U), 0U}).status ==
          g::ReferencePredictionSeedStatus::retained_slot_mismatch);
}

TEST_CASE("Immutable reference wire command maps to dry-walk movement state",
          "[prediction-command]") {
    g::GoldSrcWireUserCmd wire;
    wire.msec = 20U;
    wire.forward = 400;
    wire.side = -120;
    wire.angle_turns[1U] = 16'384U;
    const auto command = g::reference_dry_walk_movement_command(
        f::sequence(7U), wire);
    REQUIRE(command);
    CHECK(command.state->command_sequence().value() == 7U);
    CHECK(command.state->msec() == 20U);
    CHECK(command.state->forward_move() == 400.0F);
    CHECK(command.state->side_move() == -120.0F);
    CHECK(command.state->view_angles()[1U] == 90.0F);
    CHECK(command.state->compatibility_profile() ==
          g::GoldSrcUserCmdCompatibilityProfile::
              public_goldsrc48_dry_walk_prediction_v1);
    wire.buttons = g::kReferenceGoldSrcButtonJump;
    CHECK_FALSE(g::reference_dry_walk_movement_command(
        f::sequence(7U), wire));
    // H4: action-capable reference adaptation must accept only the two
    // supported immutable wire action bits, without changing the v1 gate.
    CHECK(g::reference_jump_duck_movement_command(
        f::sequence(7U), wire));
    wire.buttons = g::kReferenceGoldSrcButtonDuck;
    CHECK(g::reference_jump_duck_movement_command(
        f::sequence(7U), wire));
    wire.buttons = g::kReferenceGoldSrcButtonJump |
                   g::kReferenceGoldSrcButtonDuck;
    CHECK(g::reference_jump_duck_movement_command(
        f::sequence(7U), wire));
    wire.buttons = g::kSyntheticGoldSrcButtonUse;
    CHECK_FALSE(g::reference_jump_duck_movement_command(
        f::sequence(7U), wire));
    wire.buttons = g::kReferenceGoldSrcButtonAttack |
                   g::kReferenceGoldSrcButtonReload |
                   g::kReferenceGoldSrcButtonJump;
    CHECK_FALSE(g::reference_jump_duck_movement_command(
        f::sequence(7U), wire));
    const auto weapon_inert = g::reference_jump_duck_weapon_movement_command(
        f::sequence(7U), wire);
    REQUIRE(weapon_inert);
    CHECK(weapon_inert.state->buttons() == wire.buttons);
    CHECK(weapon_inert.state->compatibility_profile() ==
          g::GoldSrcUserCmdCompatibilityProfile::
              public_goldsrc48_jump_duck_weapon_prediction_v3);
    wire.buttons = g::kSyntheticGoldSrcButtonAttack2;
    CHECK_FALSE(g::reference_jump_duck_weapon_movement_command(
        f::sequence(7U), wire));
    wire.buttons = 0U;
    wire.msec = 51U;
    CHECK_FALSE(g::reference_dry_walk_movement_command(
        f::sequence(7U), wire));
}

TEST_CASE("SDL A/D reaches reference history and actual encoded send",
          "[reference-transmission][inputcommandwire][strafe]") {
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    hlclient::platform::detail::SdlPlatformEventTranslator translator;
    hlclient::input::InputStateTracker tracker;
    auto bindings = hlclient::gameplay_input::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    hlclient::app::LiveVisualCameraController camera;
    g::GoldSrcUserCmdInputAdapter adapter;
    std::uint32_t sequence = 0U;

    auto key = [](SDL_Scancode scancode, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = scancode;
        return event;
    };
    auto mouse = [](float xrel) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.xrel = xrel;
        return event;
    };
    auto focus = [](bool gained) {
        SDL_Event event{};
        event.type = gained ? SDL_EVENT_WINDOW_FOCUS_GAINED
                            : SDL_EVENT_WINDOW_FOCUS_LOST;
        return event;
    };
    auto frame = [&](std::initializer_list<SDL_Event> events,
                     float expected_forward, float expected_side,
                     std::int16_t wire_forward, std::int16_t wire_side,
                     double expected_yaw) {
        tracker.begin_frame();
        for (const auto &native : events) {
            const auto translated = translator.translate(native);
            REQUIRE(translated);
            const auto *input = std::get_if<hlclient::input::InputEvent>(&*translated);
            REQUIRE(input);
            tracker.apply_event(*input);
        }
        if (sequence == 0U)
            tracker.apply_event(hlclient::input::InputEvent::capture_acquired());
        const auto snapshot = tracker.publish_snapshot();
        auto built = hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
            snapshot, *bindings.bindings,
            hlclient::gameplay_input::MouseLookConfig{}, 0.02);
        REQUIRE(built);
        const auto &intent = *built.intent;
        CHECK(intent.forward_axis() == expected_forward);
        CHECK(intent.side_axis() == expected_side);
        REQUIRE(camera.apply_local_look(intent));
        CHECK(camera.yaw_degrees() == expected_yaw);
        ++sequence;
        hlclient::gameplay_camera::GameplayCameraStateCreateInfo camera_info;
        camera_info.yaw_degrees = camera.yaw_degrees();
        camera_info.pitch_degrees = camera.pitch_degrees();
        auto command_camera = hlclient::gameplay_camera::GameplayCameraState::create(
            camera_info);
        REQUIRE(command_camera);
        g::GoldSrcUserCmdBuildContext context;
        context.command_sequence = f::sequence(sequence);
        context.command_msec = 20U;
        context.command_sample_duration_nanoseconds = 20'000'000U;
        context.command_sample_time_nanoseconds = sequence * 20'000'000LL;
        context.movement_speeds = g::live_visual_movement_speeds(
            true, intent.forward_axis(), intent.side_axis(), 100.0F);
        auto command = adapter.build_reference_wire(intent, *command_camera.state,
                                                    context);
        REQUIRE(command);
        CHECK(command.command->forward == wire_forward);
        CHECK(command.command->side == wire_side);
        CHECK(command.command->up == 0);
        const std::uint16_t expected_direction_buttons =
            (snapshot.key_held(hlclient::input::PhysicalKey::a) ? 512U : 0U) |
            (snapshot.key_held(hlclient::input::PhysicalKey::d) ? 1024U : 0U) |
            (snapshot.key_held(hlclient::input::PhysicalKey::w) ? 8U : 0U);
        CHECK(command.command->buttons == expected_direction_buttons);
        CHECK(command.command->impulse == 0U);
        REQUIRE(stage.queue_reference_command(f::sequence(sequence),
                                              *command.command, 17U));
        auto history = stage.history();
        REQUIRE(history.find(f::sequence(sequence)));
        CHECK(history.find(f::sequence(sequence))->reference_command->side ==
              wire_side);
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} +
                                       (sequence + 1U) * 20ms));
        REQUIRE_FALSE(transport.sent.empty());
        auto sent = decode(transport.sent.back());
        REQUIRE(sent.new_count == 1U);
        CHECK(sent.commands.back().value.forward == wire_forward);
        CHECK(sent.commands.back().value.side == wire_side);
        CHECK(sent.commands.back().value.angle_turns[1] ==
              command.command->angle_turns[1]);
        tracker.end_frame();
    };

    frame({focus(true), key(SDL_SCANCODE_A, true)}, 0, -1, 0, -100, 0);
    frame({}, 0, -1, 0, -100, 0); // held across frames
    frame({key(SDL_SCANCODE_A, false)}, 0, 0, 0, 0, 0);
    frame({key(SDL_SCANCODE_D, true)}, 0, 1, 0, 100, 0);
    frame({}, 0, 1, 0, 100, 0);
    frame({key(SDL_SCANCODE_A, true)}, 0, 0, 0, 0, 0); // A+D
    frame({key(SDL_SCANCODE_D, false), key(SDL_SCANCODE_W, true)},
          1, -1, 70, -70, 0);
    frame({key(SDL_SCANCODE_A, false), key(SDL_SCANCODE_D, true)},
          1, 1, 70, 70, 0);
    frame({key(SDL_SCANCODE_W, false), mouse(-900)}, 0, 1, 0, 100, 90);
    frame({key(SDL_SCANCODE_D, false), key(SDL_SCANCODE_A, true)},
          0, -1, 0, -100, 90);
    frame({mouse(-900)}, 0, -1, 0, -100, -180);
    frame({key(SDL_SCANCODE_A, false), key(SDL_SCANCODE_D, true)},
          0, 1, 0, 100, -180);
    frame({key(SDL_SCANCODE_A, true), key(SDL_SCANCODE_D, false)},
          0, -1, 0, -100, -180);
    frame({focus(false)}, 0, 0, 0, 0, -180);
    const auto scripted = g::live_visual_movement_speeds(false, 1, 0, 100);
    CHECK(scripted.forward_speed == 100);
    CHECK(scripted.side_speed == 0);
}

TEST_CASE("SDL Left Shift changes actual reference command without a speed wire bit",
          "[reference-transmission][inputcommandwire][speed]") {
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    hlclient::platform::detail::SdlPlatformEventTranslator translator;
    hlclient::input::InputStateTracker tracker;
    auto bindings = hlclient::gameplay_input::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    g::GoldSrcUserCmdInputAdapter adapter;
    auto key = [](SDL_Scancode scancode, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = scancode;
        return event;
    };
    auto focus = [](bool gained) {
        SDL_Event event{};
        event.type = gained ? SDL_EVENT_WINDOW_FOCUS_GAINED
                            : SDL_EVENT_WINDOW_FOCUS_LOST;
        return event;
    };
    std::uint32_t sequence = 0U;
    const auto frame = [&](std::initializer_list<SDL_Event> events,
                           std::int16_t forward, std::int16_t side,
                           std::uint16_t buttons) {
        tracker.begin_frame();
        for (const auto &event : events) {
            const auto translated = translator.translate(event);
            REQUIRE(translated);
            const auto *input = std::get_if<hlclient::input::InputEvent>(&*translated);
            REQUIRE(input);
            tracker.apply_event(*input);
        }
        const auto snapshot = tracker.publish_snapshot();
        auto intent = hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
            snapshot, *bindings.bindings,
            hlclient::gameplay_input::MouseLookConfig{}, 0.02);
        REQUIRE(intent);
        ++sequence;
        g::GoldSrcUserCmdBuildContext context;
        context.command_sequence = f::sequence(sequence);
        context.command_msec = 20U;
        context.command_sample_duration_nanoseconds = 20'000'000U;
        context.command_sample_time_nanoseconds = sequence * 20'000'000LL;
        context.movement_speeds =
            hlclient::games::halflife::make_movement_policy().movement_speeds;
        context.reference_movement.speed_key_multiplier = 0.3F;
        context.reference_button_policy = g::GoldSrcReferenceButtonPolicy::jump_duck;
        auto camera = hlclient::gameplay_camera::GameplayCameraState::create({});
        REQUIRE(camera);
        auto built = adapter.build_reference_wire(*intent.intent, *camera.state,
                                                   context);
        REQUIRE(built);
        CHECK(built.command->forward == forward);
        CHECK(built.command->side == side);
        CHECK(built.command->buttons == buttons);
        REQUIRE(stage.queue_reference_command(f::sequence(sequence),
                                              *built.command, 17U));
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} +
                                       (sequence + 1U) * 20ms));
        REQUIRE_FALSE(transport.sent.empty());
        const auto sent = decode(transport.sent.back());
        REQUIRE(sent.new_count == 1U);
        CHECK(sent.commands.back().value.forward == forward);
        CHECK(sent.commands.back().value.side == side);
        CHECK(sent.commands.back().value.buttons == buttons);
        tracker.end_frame();
    };
    frame({focus(true), key(SDL_SCANCODE_W, true)}, 400, 0, 8U);
    frame({key(SDL_SCANCODE_LSHIFT, true), key(SDL_SCANCODE_A, true),
           key(SDL_SCANCODE_SPACE, true), key(SDL_SCANCODE_LCTRL, true)},
          120, -120, 8U|512U|6U);
    frame({key(SDL_SCANCODE_LSHIFT, true)}, 120, -120, 8U|512U|6U);
    frame({key(SDL_SCANCODE_LSHIFT, false), key(SDL_SCANCODE_A, false),
           key(SDL_SCANCODE_SPACE, false), key(SDL_SCANCODE_LCTRL, false)},
          400, 0, 8U);
    frame({focus(false)}, 0, 0, 0U);
    frame({focus(true), key(SDL_SCANCODE_W, true)}, 400, 0, 8U);
}

TEST_CASE("SDL jump tap survives render frames and wire backpressure",
          "[reference-transmission][inputcommandwire][jump-duck]") {
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    hlclient::platform::detail::SdlPlatformEventTranslator translator;
    hlclient::input::InputStateTracker tracker;
    g::LiveVisualButtonLatch latch;
    auto bindings =
        hlclient::gameplay_input::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    auto native_key = [](const SDL_Scancode scancode, const bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = scancode;
        return event;
    };
    auto frame = [&](const std::initializer_list<SDL_Event> events,
                     const bool initial_focus = false) {
        tracker.begin_frame();
        if (initial_focus)
            tracker.apply_event(hlclient::input::InputEvent::focus_gained());
        for (const auto &native : events) {
            const auto translated = translator.translate(native);
            REQUIRE(translated);
            const auto *event =
                std::get_if<hlclient::input::InputEvent>(&*translated);
            REQUIRE(event);
            tracker.apply_event(*event);
        }
        const auto snapshot = tracker.publish_snapshot();
        auto built = hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(
            snapshot, *bindings.bindings,
            hlclient::gameplay_input::MouseLookConfig{}, 0.02);
        REQUIRE(built);
        latch.observe(built.intent->pressed_buttons(), built.intent->focused());
        tracker.end_frame();
        return std::move(*built.intent);
    };
    auto sample = [&](const hlclient::gameplay_input::GameplayInputIntent &intent,
                      const std::uint32_t sequence, const std::uint16_t expected) {
        g::GoldSrcUserCmdBuildContext context;
        context.command_sequence = f::sequence(sequence);
        context.command_msec = 20U;
        context.command_sample_duration_nanoseconds = 20'000'000U;
        context.reference_button_policy =
            g::GoldSrcReferenceButtonPolicy::jump_duck;
        context.one_shot_buttons = latch.pending();
        auto camera = hlclient::gameplay_camera::GameplayCameraState::create({});
        REQUIRE(camera);
        auto built = g::GoldSrcUserCmdInputAdapter{}.build_reference_wire(
            intent, *camera.state, context);
        REQUIRE(built);
        CHECK(built.command->buttons == expected);
        CHECK(built.command->up == 0);
        CHECK(built.command->impulse == 0U);
        REQUIRE(stage.queue_reference_command(f::sequence(sequence),
                                              *built.command, 17U));
        if (built.one_shot_plan) {
            REQUIRE(built.one_shot_plan->commit_after_history_insert(
                f::sequence(sequence)));
            latch.consume_after_history_insert(
                built.one_shot_plan->consumes_buttons());
        }
        CHECK(stage.history().find(f::sequence(sequence))
                  ->reference_command->buttons == expected);
    };

    // Both physical edges occur before the first 20 ms command sample.
    auto short_tap = frame({native_key(SDL_SCANCODE_SPACE, true),
                            native_key(SDL_SCANCODE_SPACE, false)}, true);
    CHECK(short_tap.pressed_buttons() == 1U);
    CHECK(short_tap.released_buttons() == 1U);
    CHECK(latch.pending() == 1U);
    auto idle_render = frame({});
    CHECK(latch.pending() == 1U);
    sample(idle_render, 1U, 2U);
    CHECK(latch.pending() == 0U);
    transport.block = true;
    static_cast<void>(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    CHECK(transport.sent.empty());
    CHECK(stage.history().find(f::sequence(1))->reference_command->buttons == 2U);
    transport.block = false;
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
    REQUIRE(transport.sent.size() == 1U);
    CHECK(decode(transport.sent.back()).commands.back().value.buttons == 2U);

    sample(frame({}), 2U, 0U);
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 4ms));
    REQUIRE(transport.sent.size() == 2U);
    const auto after_release = decode(transport.sent.back());
    CHECK(after_release.commands.back().value.buttons == 0U);
    REQUIRE(after_release.backup_count != 0U);
    CHECK(after_release.commands.front().value.buttons == 2U);

    auto held = frame({native_key(SDL_SCANCODE_SPACE, true),
                       native_key(SDL_SCANCODE_LCTRL, true),
                       native_key(SDL_SCANCODE_W, true),
                       native_key(SDL_SCANCODE_A, true)});
    sample(held, 3U, 8U|512U|6U);
    sample(frame({native_key(SDL_SCANCODE_SPACE, true)}), 4U, 8U|512U|6U);
    sample(frame({native_key(SDL_SCANCODE_SPACE, false),
                  native_key(SDL_SCANCODE_LCTRL, false)}), 5U, 8U|512U);
    auto interrupted = frame({native_key(SDL_SCANCODE_SPACE, true)});
    CHECK(interrupted.pressed_buttons() == 1U);
    CHECK(latch.pending() == 1U);
    SDL_Event focus_lost{};
    focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    sample(frame({focus_lost}), 6U, 0U);
    CHECK(latch.pending() == 0U);
}

TEST_CASE("Reference history preserves wire values identities generation and "
          "pressure",
          "[reference-transmission][usercmd][history]") {
    auto c = config();
    c.history.maximum_entries = 3;
    c.history.protected_backup_window = 1;
    g::GoldSrcUserCmdHistoryBuilder history{c.history};
    g::GoldSrcWireUserCmd command;
    command.side = -2047;
    command.buttons = 65535;
    command.impulse = 9;
    CHECK_FALSE(history.insert(f::default_state(1)));
    CHECK_FALSE(history.insert(f::sequence(1), command, 18));
    for (unsigned i = 1; i <= 3; ++i)
        REQUIRE(history.insert(f::sequence(i), command, 17));
    const auto revision = history.revision();
    CHECK_FALSE(history.insert(f::sequence(3), command, 17));
    auto full = history.insert(f::sequence(4), command, 17);
    REQUIRE_FALSE(full);
    CHECK(full.error->code == g::GoldSrcUserCmdHistoryErrorCode::history_full);
    CHECK(history.revision() == revision);
    CHECK(history.unsent_count() == 3);
    auto snapshot = history.publish();
    command.impulse = 0;
    CHECK(snapshot.entries()[0].reference_command->impulse == 9);
    CHECK_FALSE(snapshot.entries()[0].command);
    REQUIRE(Access::set_transmission_counts(history, f::sequence(1), 1, 0));
    REQUIRE(history.insert(f::sequence(4), command, 17));
    CHECK(history.find(f::sequence(1)) == nullptr);
    g::GoldSrcUserCmdHistoryBuilder synthetic;
    CHECK_FALSE(synthetic.insert(f::sequence(1), command, 1));
}

TEST_CASE("Reference planner owns bounded plans rejects foreign stale and "
          "exhausted commits",
          "[reference-transmission][usercmd][planner]") {
    Transport t;
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    auto c = config();
    g::GoldSrcUserCmdHistoryBuilder history{c.history}, foreign{c.history};
    REQUIRE(history.insert(f::sequence(1), {}, 17));
    REQUIRE(foreign.insert(f::sequence(1), {}, 17));
    g::GoldSrcUserCmdPacketPlanner planner{c.planner}, other{c.planner};
    auto context = driver.prepare_unreliable_context();
    REQUIRE(context);
    CHECK_FALSE(planner.prepare(history.publish(), binding(),
                                2)); // No sequence-only bypass.
    auto prepared = planner.prepare(history.publish(), binding(), *context.plan);
    REQUIRE(prepared);
    CHECK(history.unsent_count() == 1);
    CHECK(t.sent.empty());
    REQUIRE(prepared.plan->reference_message());
    CHECK(prepared.plan->ordered_commands().empty());
    constexpr std::array literal{std::byte{2}, std::byte{4},    std::byte{0xfc},
                                 std::byte{2}, std::byte{0x19}, std::byte{0x50},
                                 std::byte{2}};
    CHECK(std::equal(literal.begin(), literal.end(),
                     prepared.plan->encoded_bytes().begin(),
                     prepared.plan->encoded_bytes().end()));
    CHECK_FALSE(other.preflight(history, *prepared.plan));
    CHECK_FALSE(planner.preflight(foreign, *prepared.plan));
    REQUIRE(history.insert(f::sequence(2), {}, 17));
    CHECK_FALSE(planner.preflight(history, *prepared.plan));
    REQUIRE(planner.abandon(std::move(*prepared.plan)));
    CHECK_FALSE(planner.abandon(std::move(*prepared.plan)));
    auto again = planner.prepare(history.publish(), binding(), *context.plan);
    REQUIRE(again);
    Access::set_history_revision(history, UINT64_MAX);
    CHECK_FALSE(planner.preflight(history, *again.plan));
    CHECK(t.sent.empty());
}

TEST_CASE("Reference batching reduces oldest backups then newest new tail "
          "without skipping",
          "[reference-transmission][usercmd][capacity]") {
    Transport t;
    g::NetchanDriverConfig dc;
    dc.maximum_unreliable_payload_size = 7;
    g::NetchanDriver driver{t, t.remote, dc};
    activate(driver, t);
    auto c = config();
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, c};
    for (unsigned i = 1; i <= 4; ++i)
        REQUIRE(stage.queue_reference_command(f::sequence(i), {}, 17));
    for (unsigned i = 0; i < 4; ++i) {
        while (stage.poll_event()) {
        }
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + (i + 2) * 1ms));
        REQUIRE(t.sent.size() == i + 1);
        auto move = decode(t.sent.back());
        CHECK(move.new_count == 1);
        CHECK(move.backup_count == 0);
        CHECK(move.bytes.size() == 7);
        auto history = stage.history();
        for (unsigned j = 1; j <= 4; ++j)
            CHECK(history.find(f::sequence(j))->new_transmission_count ==
                  (j <= i + 1 ? 1U : 0U));
    }
    g::GoldSrcWireUserCmd larger;
    larger.forward = 400;
    REQUIRE(stage.queue_reference_command(f::sequence(5), larger, 17));
    while (stage.poll_event()) {
    }
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 8ms));
    CHECK(stage.state() == g::GoldSrcUserCmdTransmissionState::unreliable_backpressure);
    CHECK(stage.history().find(f::sequence(5))->new_transmission_count == 0);
    CHECK(t.sent.size() == 4);
}

TEST_CASE("Reference would-block retains ownership and impulse with exactly "
          "one history commit",
          "[reference-transmission][usercmd][ownership]") {
    Transport t;
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    g::GoldSrcWireUserCmd command;
    command.impulse = 7;
    command.buttons = 0xffff;
    command.forward = -123;
    REQUIRE(stage.queue_reference_command(f::sequence(1), command, 17));
    t.block = true;
    for (unsigned i = 2; i < 5; ++i) {
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + i * 1ms));
        CHECK(stage.transmitted_packet_count() == 0);
        CHECK(stage.history().revision() == 1);
        CHECK(driver.session().state().next_outgoing_sequence == seq(2));
    }
    auto blocked = stage.queue_reference_command(f::sequence(2), command, 17);
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error->code == g::GoldSrcUserCmdHistoryErrorCode::pending_submission);
    CHECK_FALSE(driver.submit_unreliable(std::array{std::byte{1}}));
    t.block = false;
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 5ms));
    REQUIRE(t.sent.size() == 1);
    CHECK(t.attempts == 4);
    CHECK(decode(t.sent[0]).commands[0].value == command);
    CHECK(stage.history().revision() == 2);
    CHECK(stage.history().find(f::sequence(1))->new_transmission_count == 1);
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 6ms));
    CHECK(t.sent.size() == 1);
}

TEST_CASE("Reference stale context is reencoded and terminal send failure "
          "never fabricates submission",
          "[reference-transmission][usercmd][failure]") {
    Transport t;
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    auto c = config();
    c.maximum_transmission_phases_per_update = 1;
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, c};
    REQUIRE(stage.queue_reference_command(f::sequence(1), {}, 17));
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    SECTION("reliable composition changed") {
        REQUIRE(driver.queue_reliable(std::array{std::byte{1}}));
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
        CHECK(t.sent.empty());
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 4ms));
        REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 5ms));
        REQUIRE(t.sent.size() == 1);
        CHECK(decode(t.sent[0], 17, 1).commands[0].value == g::GoldSrcWireUserCmd{});
    }
    SECTION("terminal failure") {
        t.fail = true;
        CHECK_FALSE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
        CHECK(stage.terminal());
        CHECK(stage.transmitted_packet_count() == 0);
        CHECK(t.sent.empty());
        CHECK(stage.history().find(f::sequence(1))->new_transmission_count == 0);
        CHECK(driver.cleanup_count() == 1);
    }
    SECTION("history changed before send") {
        Access::set_history_revision(Access::history(stage), UINT64_MAX);
        CHECK_FALSE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
        CHECK(t.sent.empty());
        CHECK(stage.terminal());
    }
    SECTION("planner counters changed before send") {
        Access::set_planner_revision(Access::planner(stage), UINT64_MAX);
        CHECK_FALSE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
        CHECK(t.sent.empty());
    }
}

TEST_CASE("Reference readiness remains explicit local and restart generation scoped",
          "[reference-transmission][usercmd][gate]") {
    Transport t;
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    g::GoldSrcUserCmdTransmissionStage pending{driver, binding(), {}, config()};
    CHECK_FALSE(pending.valid_configuration());
    CHECK_FALSE(pending.queue_reference_command(f::sequence(1), {}, 17));
    CHECK_FALSE(pending.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    CHECK(t.sent.empty());
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    CHECK_FALSE(stage.queue_reference_command(f::sequence(1), {}, 18));
    REQUIRE(stage.queue_reference_command(f::sequence(1), {}, 17));
    stage.close(g::NetchanDriverTimePoint{} + 3ms);
    CHECK_FALSE(stage.queue_reference_command(f::sequence(2), {}, 17));
    Transport t2;
    g::NetchanDriver driver2{t2, t2.remote};
    activate(driver2, t2);
    g::GoldSrcUserCmdTransmissionStage restarted{driver2, binding(), ready, config(18)};
    CHECK_FALSE(restarted.queue_reference_command(f::sequence(1), {}, 17));
    REQUIRE(restarted.queue_reference_command(f::sequence(1), {}, 18));
    REQUIRE(restarted.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    REQUIRE(t2.sent.size() == 1);
    CHECK(decode(t2.sent[0], 18).source.generation == 18);
}

TEST_CASE("Production handoff is generation scoped and commits only after the shared driver pump",
          "[reference-transmission][usercmd][production][shared-driver]") {
    Transport t;
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    auto c = config();
    c.externally_owned_driver_update = true;
    const g::GoldSrcUserCmdSessionPrerequisite production_ready{
        g::GoldSrcUserCmdSessionPrerequisiteProfile::
            production_live_runtime_ready_v1,
        true,
        17U};
    g::GoldSrcUserCmdTransmissionStage stage{
        driver, binding(), production_ready, c};
    REQUIRE(stage.valid_configuration());
    REQUIRE(stage.queue_reference_command(f::sequence(1), {}, 17));

    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    CHECK(t.sent.empty());
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(stage.history().find(f::sequence(1))->new_transmission_count == 0U);

    driver.update(g::NetchanDriverTimePoint{} + 2ms);
    REQUIRE(t.sent.size() == 1U);
    CHECK(stage.transmitted_packet_count() == 0U);
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    CHECK(stage.transmitted_packet_count() == 1U);
    CHECK(stage.new_command_submission_count() == 1U);
    CHECK(stage.history().find(f::sequence(1))->new_transmission_count == 1U);

    const g::GoldSrcUserCmdSessionPrerequisite stale_ready{
        g::GoldSrcUserCmdSessionPrerequisiteProfile::
            production_live_runtime_ready_v1,
        true,
        16U};
    g::GoldSrcUserCmdTransmissionStage stale{
        driver, binding(), stale_ready, c};
    CHECK_FALSE(stale.valid_configuration());
    CHECK_FALSE(stale.queue_reference_command(f::sequence(1), {}, 17));
}

TEST_CASE("Post-driver reference preparation exposes only its owned submission and clears on receipt or teardown",
          "[e311][fullscreen-usercmd][reference-transmission][usercmd][production][shared-driver][ownership]") {
    Transport transport;
    g::NetchanDriver driver{transport, transport.remote};
    activate(driver, transport);
    auto transmission_config = config();
    transmission_config.externally_owned_driver_update = true;
    transmission_config.timeout = 5ms;
    const g::GoldSrcUserCmdSessionPrerequisite production_ready{
        g::GoldSrcUserCmdSessionPrerequisiteProfile::production_live_runtime_ready_v1,
        true, 17U};
    g::GoldSrcUserCmdTransmissionStage stage{
        driver, binding(), production_ready, transmission_config};
    REQUIRE(stage.valid_configuration());
    CHECK_FALSE(stage.has_pending_reference_submission());
    // A legitimate already-owned clc_nop suffix makes the BEFORE phase wait.
    // The one shared pump drains it; the AFTER phase then binds a move which
    // has not yet had its own pump. This is distinct from unplanned history.
    REQUIRE(driver.submit_unreliable(std::array{std::byte{1}}));
    g::GoldSrcWireUserCmd command;
    command.msec = 20U;
    for (unsigned sequence = 1U; sequence <= 3U; ++sequence)
        REQUIRE(stage.queue_reference_command(f::sequence(sequence), command, 17U));
    CHECK_FALSE(stage.has_pending_reference_submission());
    const auto first = g::NetchanDriverTimePoint{} + 2ms;
    REQUIRE(stage.update_reference(first));
    CHECK(stage.state() == g::GoldSrcUserCmdTransmissionState::unreliable_backpressure);
    CHECK_FALSE(stage.has_pending_reference_submission());
    driver.update(first);
    REQUIRE(transport.sent.size() == 1U);
    REQUIRE(stage.update_reference(first));
    CHECK(stage.has_pending_reference_submission());
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(stage.new_command_submission_count() == 0U);
    const auto rejected = stage.queue_reference_command(f::sequence(4U), command, 17U);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error->code == g::GoldSrcUserCmdHistoryErrorCode::pending_submission);
    CHECK(stage.history().size() == 3U);

    SECTION("A would-block pump preserves ownership and later commits each new command once") {
        transport.block = true;
        REQUIRE(stage.update_reference(first + 1ms));
        driver.update(first + 1ms);
        REQUIRE(stage.update_reference(first + 1ms));
        CHECK(stage.has_pending_reference_submission());
        CHECK(stage.new_command_submission_count() == 0U);
        CHECK(transport.sent.size() == 1U);
        transport.block = false;
        REQUIRE(stage.update_reference(first + 2ms));
        driver.update(first + 2ms);
        CHECK(stage.has_pending_reference_submission()); // Receipt not accounted yet.
        REQUIRE(stage.update_reference(first + 2ms));
        CHECK_FALSE(stage.has_pending_reference_submission());
        REQUIRE(transport.sent.size() == 2U);
        CHECK(stage.transmitted_packet_count() == 1U);
        CHECK(stage.new_command_submission_count() == 3U);
        const auto sent = decode(transport.sent.back());
        CHECK(sent.new_count == 3U);
        CHECK(sent.checksum_matched);
        for (unsigned sequence = 1U; sequence <= 3U; ++sequence)
            CHECK(stage.history().find(f::sequence(sequence))->new_transmission_count == 1U);
        REQUIRE(stage.queue_reference_command(f::sequence(4U), command, 17U));
        CHECK_FALSE(stage.has_pending_reference_submission());
        REQUIRE(stage.update_reference(first + 3ms));
        driver.update(first + 3ms);
        REQUIRE(stage.update_reference(first + 3ms));
        CHECK_FALSE(stage.has_pending_reference_submission());
        CHECK(stage.new_command_submission_count() == 4U);
        CHECK(decode(transport.sent.back()).new_count == 1U);
        const auto transmitted = transport.sent.size();
        REQUIRE(stage.update_reference(first + 3ms));
        CHECK(transport.sent.size() == transmitted);
        CHECK(stage.new_command_submission_count() == 4U);
    }
    SECTION("Cancellation abandons the pending capability without sending movement") {
        stage.cancel(first + 1ms);
        CHECK(stage.terminal());
        CHECK_FALSE(stage.has_pending_reference_submission());
        CHECK_FALSE(stage.queue_reference_command(f::sequence(4U), command, 17U));
        CHECK_FALSE(stage.update_reference(first + 2ms));
        CHECK(transport.sent.size() == 1U);
        CHECK(stage.new_command_submission_count() == 0U);
    }
    SECTION("Close abandons the pending capability without sending movement") {
        stage.close(first + 1ms);
        CHECK(stage.terminal());
        CHECK_FALSE(stage.has_pending_reference_submission());
        CHECK_FALSE(stage.queue_reference_command(f::sequence(4U), command, 17U));
        CHECK_FALSE(stage.update_reference(first + 2ms));
        CHECK(transport.sent.size() == 1U);
        CHECK(stage.new_command_submission_count() == 0U);
    }
    SECTION("A real stalled-progress timeout remains terminal") {
        CHECK_FALSE(stage.update_reference(first + 6ms));
        REQUIRE(stage.last_error());
        CHECK(stage.last_error()->code == g::GoldSrcUserCmdTransmissionErrorCode::timed_out);
        CHECK(stage.terminal());
        CHECK_FALSE(stage.has_pending_reference_submission());
        CHECK(transport.sent.size() == 1U);
        CHECK(stage.new_command_submission_count() == 0U);
    }
}

TEST_CASE("Reference production stage sends real loopback UDP to "
          "invocation-owned peer",
          "[reference-transmission][udp-integration]") {
    auto result = hlclient::tools::reference_move_transport_self_test();
    INFO(result.failure);
    REQUIRE(result.passed);
    CHECK(result.packets_sent == 6);
    CHECK(result.packets_received == 6);
    CHECK(result.new_submitted == 6);
    CHECK(result.backup_submitted == 9);
    CHECK(result.checksum_matched == 5);
    CHECK(result.dropped == 1);
    CHECK(result.would_block == 1);
    CHECK(result.retries == 1);
    CHECK(result.stale_contexts == 1);
    CHECK(result.expected_commands == 12);
    CHECK(result.history_revision == 12);
    CHECK(result.cleanup);
}

TEST_CASE(
    "Reference test prerequisite rejects an external endpoint even on a fake transport",
    "[reference-transmission][usercmd][gate]") {
    Transport t;
    t.remote = n::NetworkAddress{0xc0000201U, 32101};
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, config()};
    CHECK_FALSE(stage.queue_reference_command(f::sequence(1), {}, 17));
    CHECK_FALSE(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    CHECK(t.sent.empty());
}

TEST_CASE("Reference checksum follows existing session arithmetic across sequence wrap",
          "[reference-transmission][usercmd][wrap]") {
    const auto maximum = g::kNetchanSequenceMask;
    g::NetchanSession session{{seq(maximum - 1), seq(maximum - 2), seq(maximum - 1),
                               seq(maximum - 2), false, false}};
    auto incoming = session.inspect_incoming({{seq(0), {}}, {seq(maximum - 2), false}});
    REQUIRE(incoming);
    REQUIRE(session.commit_incoming(std::move(*incoming.inspection)));
    auto ack = session.prepare_first_acknowledgement();
    REQUIRE(ack);
    REQUIRE(session.commit_first_acknowledgement(std::move(*ack.transaction)));
    const g::GoldSrcReferenceClientMoveCodec codec{binding(), 17};
    for (unsigned i = 0; i < 2; ++i) {
        g::ReferenceMoveSource source;
        source.generation = 17;
        source.sequence = session.state().next_outgoing_sequence.value();
        CHECK(source.sequence == (i == 0 ? maximum : 0U));
        const std::array commands{g::GoldSrcWireUserCmd{}};
        auto message = codec.encode(commands, 0, 0, source);
        REQUIRE(message);
        CHECK(message.message->checksum == (i == 0 ? 0xea : 0xcc));
        auto plan = session.prepare_outgoing_packet(message.message->bytes);
        REQUIRE(plan);
        auto packet = g::encode_client_to_server_netchan_packet(plan.plan->packet());
        REQUIRE(packet);
        CHECK(decode(*packet.datagram).commands[0].value == commands[0]);
        REQUIRE(session.commit_outgoing_send(std::move(*plan.plan)));
    }
    CHECK(session.state().next_outgoing_sequence == seq(1));
}

TEST_CASE("Reference planner honors the sixty-two command wire ceiling independently "
          "of synthetic limits",
          "[reference-transmission][usercmd][limits]") {
    Transport t;
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    auto c = config();
    c.planner.desired_backup_commands = 0;
    c.planner.maximum_backup_commands = 0;
    c.planner.maximum_new_commands = 62;
    c.planner.maximum_commands_per_packet = 62;
    g::GoldSrcUserCmdHistoryBuilder history{c.history};
    for (unsigned i = 1; i <= 62; ++i)
        REQUIRE(history.insert(f::sequence(i), {}, 17));
    g::GoldSrcUserCmdPacketPlanner planner{c.planner};
    REQUIRE(planner.valid_configuration());
    auto context = driver.prepare_unreliable_context();
    REQUIRE(context);
    auto plan = planner.prepare(history.publish(), binding(), *context.plan);
    REQUIRE(plan);
    CHECK(plan.plan->new_command_count() == 62);
    CHECK(plan.plan->expected_encoded_bytes() == 68);
    c.planner.maximum_new_commands = 63;
    c.planner.maximum_commands_per_packet = 63;
    CHECK_FALSE(g::valid_goldsrc_usercmd_packet_planner_config(c.planner));
    REQUIRE(Access::set_transmission_counts(history, f::sequence(1), 1, UINT32_MAX));
    c = config();
    g::GoldSrcUserCmdPacketPlanner backups{c.planner};
    auto exhausted = backups.prepare(history.publish(), binding(), *context.plan);
    REQUIRE_FALSE(exhausted);
    CHECK(exhausted.error->history_code ==
          g::GoldSrcUserCmdHistoryErrorCode::transmission_count_overflow);
    CHECK(t.sent.empty());
}

TEST_CASE(
    "Reference stale ordinary context rebuilds with retained fragment composition",
    "[reference-transmission][usercmd][fragment]") {
    Transport t;
    g::NetchanDriver driver{t, t.remote};
    activate(driver, t);
    auto c = config();
    c.maximum_transmission_phases_per_update = 1;
    g::GoldSrcUserCmdTransmissionStage stage{driver, binding(), ready, c};
    REQUIRE(stage.queue_reference_command(f::sequence(1), {}, 17));
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 2ms));
    const std::vector<std::byte> reliable(5000, std::byte{0x41});
    REQUIRE(driver.queue_reliable(reliable));
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 3ms));
    CHECK(t.sent.empty());
    while (stage.poll_event()) {
    }
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 4ms));
    REQUIRE(stage.update_reference(g::NetchanDriverTimePoint{} + 5ms));
    REQUIRE(t.sent.size() == 1);
    auto packet = g::decode_client_to_server_netchan_packet(t.sent[0]);
    REQUIRE(packet);
    REQUIRE(packet.packet->fragments[0]);
    CHECK(packet.packet->header.sequence.flags.fragmented);
    const auto prefix = packet.packet->fragments[0]->length;
    CHECK(std::all_of(packet.packet->payload.begin(),
                      packet.packet->payload.begin() + prefix,
                      [](std::byte b) { return b == std::byte{0x41}; }));
    g::ReferenceMoveSource source;
    source.generation = 17;
    source.sequence = packet.packet->header.sequence.sequence.value();
    auto move = g::GoldSrcReferenceClientMoveCodec{binding(), 17}.decode(
        packet.packet->payload, prefix, source);
    REQUIRE(move);
    CHECK(move.message->end_byte == packet.packet->payload.size());
    CHECK(move.message->commands[0].value == g::GoldSrcWireUserCmd{});
    CHECK(stage.history().find(f::sequence(1))->new_transmission_count == 1);
}
