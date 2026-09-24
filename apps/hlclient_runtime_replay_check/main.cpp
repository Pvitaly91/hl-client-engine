#include <hlclient/goldsrc/runtime_replay_fixture.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

namespace client = hlclient::client;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] const client::RuntimePacketEntityObservation* find_entity(
    const client::RuntimeClientObservationState& state,
    const std::uint32_t number)
{
    const auto found = std::find_if(
        state.packet_entities.begin(), state.packet_entities.end(),
        [number](const auto& value) { return value.entity_number == number; });
    return found == state.packet_entities.end() ? nullptr : &*found;
}

void print_state(
    const std::string_view step,
    const goldsrc::RuntimeReplayApplyResult& result,
    const client::ClientWorldState& world)
{
    const auto& state = *world.runtime_observation();
    const auto* entity_two = find_entity(state, 2U);
    std::cout << "step=" << step << " outcome=applied source_frame="
              << result.event->source_transport_sequence
              << " cursor=" << result.event->decoded_batch.end_cursor.byte_offset()
              << ':' << result.event->decoded_batch.end_cursor.bit_offset()
              << " entities=" << state.packet_entities.size()
              << " entity2_x=";
    if (entity_two && entity_two->origin.x) std::cout << *entity_two->origin.x;
    else std::cout << "unavailable";
    std::cout << " health=";
    if (state.receiving_client && state.receiving_client->health)
        std::cout << *state.receiving_client->health;
    else std::cout << "unavailable";
    std::cout << " slot2_clip=";
    if (state.weapon_slots.size() > 2U && state.weapon_slots[2U].clip)
        std::cout << *state.weapon_slots[2U].clip;
    else std::cout << "unavailable";
    std::cout << " revision=" << state.publication_revision
              << " hash=" << state.canonical_state_hash << '\n';
}

[[nodiscard]] int failed(const std::string_view stage)
{
    std::cerr << "result=failure stage=" << stage << '\n';
    return 1;
}

[[nodiscard]] goldsrc::RuntimeReplayFixture build_fixture(
    const goldsrc::RuntimeReplayFixtureKind kind,
    const std::uint64_t generation,
    const std::uint32_t sequence,
    const std::uint64_t identity)
{
    auto built = goldsrc::make_runtime_replay_fixture(
        {kind, generation, sequence, identity});
    if (!built || !built.fixture) {
        throw std::runtime_error{
            built.error ? built.error->context : "fixture build failed"};
    }
    return std::move(*built.fixture);
}

} // namespace

int main()
{
    try {
        auto fixture = build_fixture(
            goldsrc::RuntimeReplayFixtureKind::reference_checker,
            1U, 100U, 1'000U);
        std::cout << "profile=" << goldsrc::to_string(
                         fixture.initialization.profile)
                  << " source=" << goldsrc::to_string(
                         goldsrc::RuntimeReplaySpecificationSource::
                             public_protocol_reference)
                  << " baseline_cursor="
                  << fixture.baseline_consumed_bits / 8U << ":0\n";

        client::ClientWorldState world;
        auto opened = goldsrc::RuntimeReplaySession::initialize(
            fixture.initialization, world);
        if (!opened) return failed("initialize");

        const auto first = opened.session->apply_record(fixture.records[0]);
        if (!first) return failed("initial_mixed");
        print_state("initial_mixed", first, world);
        const auto changed = opened.session->apply_record(fixture.records[1]);
        if (!changed || !find_entity(*world.runtime_observation(), 30U) ||
            find_entity(*world.runtime_observation(), 20U)) {
            return failed("mixed_delta");
        }
        print_state("mixed_delta_add_remove", changed, world);
        const auto client_only = opened.session->apply_record(fixture.records[2]);
        if (!client_only || client_only.event->entities_observed)
            return failed("client_only");
        print_state("client_only_older_base", client_only, world);
        const auto entity_only = opened.session->apply_record(fixture.records[3]);
        if (!entity_only || entity_only.event->clientdata_observed)
            return failed("entity_only");
        print_state("entity_only_older_base", entity_only, world);

        const auto before_missing = world.runtime_observation();
        const auto missing_entity = opened.session->apply_record(fixture.records[4]);
        if (missing_entity || !missing_entity.error ||
            missing_entity.error->recovery !=
                goldsrc::RuntimeReplayRecoveryStatus::
                    entity_full_snapshot_required ||
            world.runtime_observation() != before_missing) {
            return failed("missing_entity_base");
        }
        std::cout << "step=missing_entity_base outcome=rejected typed="
                  << goldsrc::to_string(missing_entity.error->code)
                  << " recovery="
                  << goldsrc::to_string(missing_entity.error->recovery)
                  << " state=unchanged\n";
        const auto entity_recovery =
            opened.session->apply_record(fixture.records[5]);
        if (!entity_recovery) return failed("entity_recovery");
        print_state("entity_full_recovery", entity_recovery, world);

        const auto before_client_missing = world.runtime_observation();
        const auto missing_client = opened.session->apply_record(fixture.records[6]);
        if (missing_client || !missing_client.error ||
            missing_client.error->recovery !=
                goldsrc::RuntimeReplayRecoveryStatus::
                    clientdata_no_base_required ||
            world.runtime_observation() != before_client_missing) {
            return failed("missing_client_base");
        }
        std::cout << "step=missing_client_base outcome=rejected typed="
                  << goldsrc::to_string(missing_client.error->code)
                  << " recovery="
                  << goldsrc::to_string(missing_client.error->recovery)
                  << " state=unchanged\n";
        const auto client_recovery =
            opened.session->apply_record(fixture.records[7]);
        if (!client_recovery) return failed("client_recovery");
        print_state("client_no_base_recovery", client_recovery, world);

        auto malformed_record = fixture.records[7];
        malformed_record.record_identity = 1'009U;
        malformed_record.record_ordinal = 9U;
        malformed_record.payload.source_sequence = 109U;
        malformed_record.payload.source_acknowledgement = 108U;
        malformed_record.payload.bytes.push_back(std::byte{0xffU});
        malformed_record.initial_cursor =
            *goldsrc::StockRuntimeSourceCursor::create(
                0U, 0U, malformed_record.payload.bytes.size());
        const auto before_malformed = world.runtime_observation();
        const auto malformed = opened.session->apply_record(malformed_record);
        if (malformed || !malformed.error ||
            malformed.error->decoder_error !=
                goldsrc::PacketEntityDecodeErrorCode::unsupported_opcode ||
            world.runtime_observation() != before_malformed) {
            return failed("malformed_suffix");
        }
        std::cout << "step=malformed_suffix outcome=rejected typed="
                  << goldsrc::to_string(*malformed.error->decoder_error)
                  << " state=unchanged\n";

        auto generation_two = build_fixture(
            goldsrc::RuntimeReplayFixtureKind::basic_mixed,
            2U, 200U, 2'000U);
        if (opened.session->reset_generation(generation_two.initialization))
            return failed("generation_reset");
        auto stale = fixture.records[2];
        stale.record_identity = 2'009U;
        stale.record_ordinal = 1U;
        const auto stale_result = opened.session->apply_record(stale);
        if (stale_result || !stale_result.error ||
            stale_result.error->code !=
                goldsrc::RuntimeReplayErrorCode::generation_mismatch) {
            return failed("old_generation");
        }
        std::cout << "step=generation_reset outcome=applied generation=2 revision="
                  << world.runtime_publication_revision()
                  << " old_history=unavailable stale_typed="
                  << goldsrc::to_string(stale_result.error->code) << '\n';
        const auto generation_two_initial =
            opened.session->apply_record(generation_two.records[0]);
        if (!generation_two_initial) return failed("generation_two_replay");
        print_state("generation_two_initial", generation_two_initial, world);

        auto repeated_fixture = build_fixture(
            goldsrc::RuntimeReplayFixtureKind::basic_mixed,
            1U, 100U, 3'000U);
        client::ClientWorldState repeated_world;
        auto repeated = goldsrc::RuntimeReplaySession::initialize(
            repeated_fixture.initialization, repeated_world);
        if (!repeated ||
            !repeated.session->apply_record(repeated_fixture.records[0])) {
            return failed("repeat_initial");
        }
        const auto repeated_delta =
            repeated.session->apply_record(repeated_fixture.records[1]);
        if (!repeated_delta ||
            repeated_world.runtime_observation()->canonical_state_hash !=
                changed.event->canonical_state_hash) {
            return failed("deterministic_replay");
        }
        std::cout << "step=repeat_replay outcome=applied deterministic_hash="
                  << repeated_world.runtime_observation()->canonical_state_hash
                  << '\n';
        std::cout << "stock_verification=" << goldsrc::to_string(
            goldsrc::RuntimeReplayStockVerification::
                not_verified_against_stock_runtime_payload) << '\n';
        std::cout << "result=success\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "result=failure exception=" << exception.what() << '\n';
        return 1;
    }
}
