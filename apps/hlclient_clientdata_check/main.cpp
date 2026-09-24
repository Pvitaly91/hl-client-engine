#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/packet_entity_decoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

constexpr std::array kClientSchema{
    std::byte{0x0e}, std::byte{0x63}, std::byte{0x6c}, std::byte{0x69},
    std::byte{0x65}, std::byte{0x6e}, std::byte{0x74}, std::byte{0x64},
    std::byte{0x61}, std::byte{0x74}, std::byte{0x61}, std::byte{0x5f},
    std::byte{0x74}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00},
    std::byte{0xd9}, std::byte{0x43}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x44}, std::byte{0x2b}, std::byte{0x0b},
    std::byte{0x63}, std::byte{0xa3}, std::byte{0x43}, std::byte{0x03},
    std::byte{0x08}, std::byte{0x50}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
constexpr std::array kWeaponSchema{
    std::byte{0x0e}, std::byte{0x77}, std::byte{0x65}, std::byte{0x61},
    std::byte{0x70}, std::byte{0x6f}, std::byte{0x6e}, std::byte{0x5f},
    std::byte{0x64}, std::byte{0x61}, std::byte{0x74}, std::byte{0x61},
    std::byte{0x5f}, std::byte{0x74}, std::byte{0x00}, std::byte{0x02},
    std::byte{0x00}, std::byte{0xd9}, std::byte{0x43}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x6c}, std::byte{0xfb},
    std::byte{0x4a}, std::byte{0x1b}, std::byte{0x62}, std::byte{0x4b},
    std::byte{0x83}, std::byte{0x03}, std::byte{0x08}, std::byte{0x50},
    std::byte{0x00}, std::byte{0x7d}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x7d}, std::byte{0x00}, std::byte{0x00},
    std::byte{0xc8}, std::byte{0x1f}, std::byte{0x02}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x40}, std::byte{0xdb}, std::byte{0x57},
    std::byte{0x5a}, std::byte{0x12}, std::byte{0x19}, std::byte{0x00},
    std::byte{0x01}, std::byte{0x40}, std::byte{0x80}, std::byte{0x01},
    std::byte{0xe8}, std::byte{0x03}, std::byte{0x00}, std::byte{0x00},
    std::byte{0xe8}, std::byte{0x03}, std::byte{0x00}, std::byte{0x00}};
constexpr std::array kEntitySchema{
    std::byte{0x0e}, std::byte{0x78}, std::byte{0x00}, std::byte{0x01},
    std::byte{0x00}, std::byte{0xd9}, std::byte{0x0b}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xc8}, std::byte{0x03},
    std::byte{0x08}, std::byte{0x40}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
constexpr std::array kBaselines{
    std::byte{0x01}, std::byte{0x20}, std::byte{0x01}, std::byte{0x0a},
    std::byte{0x0a}, std::byte{0x20}, std::byte{0x01}, std::byte{0x14},
    std::byte{0x14}, std::byte{0x20}, std::byte{0x01}, std::byte{0x1e},
    std::byte{0x28}, std::byte{0x30}, std::byte{0x01}, std::byte{0x32},
    std::byte{0x32}, std::byte{0x20}, std::byte{0x01}, std::byte{0x3c},
    std::byte{0xff}, std::byte{0xff}, std::byte{0x41}, std::byte{0x02},
    std::byte{0x8c}, std::byte{0x00}};

constexpr std::array kInitialStream{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x0f}, std::byte{0x00}, std::byte{0x05},
    std::byte{0x01}, std::byte{0x00}};
constexpr std::array kNoBaseWeapons{
    std::byte{0x0f}, std::byte{0x12}, std::byte{0x40}, std::byte{0x46},
    std::byte{0x20}, std::byte{0x03}, std::byte{0xff}, std::byte{0x1f},
    std::byte{0xff}, std::byte{0x04}, std::byte{0x78}, std::byte{0x00}};
constexpr std::array kDeltaFrom100{
    std::byte{0x0f}, std::byte{0xc9}, std::byte{0x12}, std::byte{0xa0},
    std::byte{0x45}, std::byte{0x20}, std::byte{0x01}, std::byte{0x05},
    std::byte{0x00}};
constexpr std::array kOlderBaseFrom100{
    std::byte{0x0f}, std::byte{0xc9}, std::byte{0xf0}, std::byte{0x4f},
    std::byte{0x00}, std::byte{0x0a}, std::byte{0x00}};
constexpr std::array kMissingBase99{
    std::byte{0x0f}, std::byte{0xc7}, std::byte{0x00}};
constexpr std::array kNoBaseEmpty{
    std::byte{0x0f}, std::byte{0x00}};
constexpr std::array kMixed{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x0f}, std::byte{0x12}, std::byte{0x40},
    std::byte{0x46}, std::byte{0x20}, std::byte{0x03}, std::byte{0xff},
    std::byte{0x1f}, std::byte{0xff}, std::byte{0x04}, std::byte{0x78},
    std::byte{0x00}, std::byte{0x28}, std::byte{0x05}, std::byte{0x00},
    std::byte{0x91}, std::byte{0x80}, std::byte{0x05}, std::byte{0x12},
    std::byte{0x24}, std::byte{0xc0}, std::byte{0x42}, std::byte{0x0a},
    std::byte{0x30}, std::byte{0x48}, std::byte{0x40}, std::byte{0x08},
    std::byte{0x8a}, std::byte{0x40}, std::byte{0x02}, std::byte{0x96},
    std::byte{0x50}, std::byte{0x92}, std::byte{0x00}, std::byte{0x16},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, std::byte{0x01},
    std::byte{0x00}};
constexpr std::array kOldGenerationBase{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x0f}, std::byte{0xdd}, std::byte{0x00}};

[[nodiscard]] goldsrc::OwnedServicePayload payload(
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence)
{
    goldsrc::OwnedServicePayload result;
    result.bytes.assign(bytes.begin(), bytes.end());
    result.source_sequence = sequence;
    result.source_acknowledgement = sequence == 0U ? 0U : sequence - 1U;
    result.source_reliable = true;
    result.reassembled = true;
    result.decompressed = true;
    result.acknowledgement_reliable = true;
    result.direction = goldsrc::NetchanDirection::server_to_client;
    return result;
}

[[nodiscard]] std::shared_ptr<const goldsrc::DeltaSchemaRegistryState>
schemas()
{
    goldsrc::DeltaSchemaRegistryBuilder builder;
    for (const auto bytes : std::array{
             std::span<const std::byte>{kClientSchema},
             std::span<const std::byte>{kWeaponSchema},
             std::span<const std::byte>{kEntitySchema}}) {
        const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
        if (!parsed || !parsed.schema || !builder.insert(*parsed.schema)) {
            return {};
        }
    }
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

[[nodiscard]] std::shared_ptr<const goldsrc::EntityBaselineRegistryState>
baselines(const goldsrc::DeltaSchemaRegistryState& registry,
          const std::uint64_t generation)
{
    auto source = payload(kBaselines, 17U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    if (!cursor) {
        return {};
    }
    const auto decoded = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        goldsrc::EntityBaselineDecodeInput{
            &source, *cursor, 0U, generation, 1U, "x", "x", "x",
            kBaselines.size() * 8U},
        registry);
    if (!decoded || !decoded.registry) {
        return {};
    }
    return std::make_shared<const goldsrc::EntityBaselineRegistryState>(
        std::move(*decoded.registry));
}

[[nodiscard]] goldsrc::PacketEntityDecodeResult decode(
    goldsrc::GoldSrcPacketEntityDecoder& decoder,
    goldsrc::PacketEntitySnapshotState& state,
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence,
    const std::size_t ordinal)
{
    auto source = payload(bytes, sequence);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    if (!cursor) {
        return {};
    }
    return decoder.decode_and_apply(
        goldsrc::PacketEntityDecodeInput{
            source, *cursor, state.source_generation(), ordinal,
            "x", "x", "x"},
        state);
}

[[nodiscard]] const goldsrc::ClientDataMessageEvent* client_event(
    const goldsrc::PacketEntityDecodeResult& result)
{
    if (!result || !result.batch) {
        return nullptr;
    }
    for (const auto& event : result.batch->events) {
        if (const auto* client =
                std::get_if<goldsrc::ClientDataMessageEvent>(&event)) {
            return client;
        }
    }
    return nullptr;
}

[[nodiscard]] std::int32_t signed_value(
    const goldsrc::DeltaObjectState& object,
    const std::string_view name)
{
    const auto* field = object.find_exact(name);
    if (field == nullptr ||
        !std::holds_alternative<std::int32_t>(field->value())) {
        return 0;
    }
    return std::get<std::int32_t>(field->value());
}

void print_frame(const std::string_view scenario,
                 const goldsrc::ClientDataMessageEvent& event,
                 const goldsrc::ClientDataSnapshotState& state)
{
    const auto& frame = *event.frame;
    const auto* zero = frame.find_weapon_slot(0U);
    const auto* last = frame.find_weapon_slot(63U);
    std::cout << "scenario=" << scenario
              << " profile=" << goldsrc::to_string(frame.profile())
              << " source=" << goldsrc::to_string(event.specification_source)
              << " base=";
    if (event.resolved_base) {
        std::cout << event.resolved_base->source_transport_sequence;
    } else {
        std::cout << "default";
    }
    std::cout << " health=" << signed_value(frame.client_data(), "health")
              << " weapon_indices=";
    for (const auto index : frame.wire_updated_weapon_indices()) {
        std::cout << static_cast<unsigned>(index) << ',';
    }
    std::cout << " slot0_clip="
              << (zero ? signed_value(zero->object(), "m_iClip") : 0)
              << " slot63_clip="
              << (last ? signed_value(last->object(), "m_iClip") : 0)
              << " changed_slots="
              << frame.statistics().changed_weapon_slot_count
              << " unchanged_slots="
              << frame.statistics().unchanged_weapon_slot_count
              << " cursor=" << event.end_cursor.byte_offset() << ':'
              << event.end_cursor.bit_offset()
              << " history=" << state.history().frame_count() << '\n';
}

[[nodiscard]] int failed(const std::string_view stage)
{
    std::cerr << "result=failure stage=" << stage << '\n';
    return 1;
}

} // namespace

int main()
{
    const auto registry = schemas();
    if (!registry) {
        return failed("schema_registry");
    }
    const auto generation_one = baselines(*registry, 1U);
    if (!generation_one) {
        return failed("baseline_decode");
    }
    goldsrc::PacketEntitySnapshotState state{
        1U, 1U, registry, generation_one};
    goldsrc::GoldSrcPacketEntityDecoder decoder;

    const auto initial = decode(decoder, state, kInitialStream, 90U, 1U);
    const auto* initial_event = client_event(initial);
    if (initial_event == nullptr) {
        return failed("no_base_empty");
    }
    print_frame("no_base_empty", *initial_event, state.client_data_state());

    const auto weapons = decode(decoder, state, kNoBaseWeapons, 100U, 2U);
    const auto* weapons_event = client_event(weapons);
    if (weapons_event == nullptr) {
        return failed("no_base_weapons");
    }
    print_frame("no_base_weapons", *weapons_event, state.client_data_state());

    const auto delta = decode(decoder, state, kDeltaFrom100, 103U, 3U);
    const auto* delta_event = client_event(delta);
    if (delta_event == nullptr || !delta_event->resolved_base ||
        delta_event->resolved_base->source_transport_sequence != 100U) {
        return failed("delta_exact_base");
    }
    print_frame("delta_exact_base", *delta_event, state.client_data_state());

    const auto older = decode(decoder, state, kOlderBaseFrom100, 105U, 4U);
    const auto* older_event = client_event(older);
    if (older_event == nullptr || !older_event->resolved_base ||
        older_event->resolved_base->source_transport_sequence != 100U) {
        return failed("older_retained_base");
    }
    print_frame("older_retained_base", *older_event,
                state.client_data_state());

    const auto before_missing = state.client_data_state().current_frame();
    const auto missing = decode(decoder, state, kMissingBase99, 106U, 5U);
    if (missing || !missing.error ||
        missing.error->code != goldsrc::PacketEntityDecodeErrorCode::clientdata_failed ||
        !missing.error->clientdata_error ||
        *missing.error->clientdata_error !=
            goldsrc::ClientDataDecodeErrorCode::missing_delta_base ||
        missing.error->recovery !=
            goldsrc::PacketEntityRecoveryStatus::clientdata_no_base_required ||
        state.client_data_state().current_frame() != before_missing) {
        return failed("missing_base_transaction");
    }
    std::cout << "scenario=missing_base typed="
              << goldsrc::to_string(*missing.error->clientdata_error)
              << " recovery=" << goldsrc::to_string(missing.error->recovery)
              << " state=unchanged\n";

    const auto recovery = decode(decoder, state, kNoBaseEmpty, 107U, 6U);
    const auto* recovery_event = client_event(recovery);
    if (recovery_event == nullptr) {
        return failed("no_base_recovery");
    }
    print_frame("no_base_recovery", *recovery_event,
                state.client_data_state());

    const auto mixed = decode(decoder, state, kMixed, 110U, 7U);
    const auto* mixed_event = client_event(mixed);
    if (mixed_event == nullptr || !state.current_snapshot() ||
        state.current_snapshot()->entity_count() != 5U ||
        state.current_snapshot()->reference().value() != 110U ||
        mixed_event->frame->reference().source_transport_sequence() != 110U) {
        return failed("mixed_frame");
    }
    print_frame("mixed_client_entities", *mixed_event,
                state.client_data_state());
    std::cout << "scenario=mixed_client_entities entity_count="
              << state.current_snapshot()->entity_count()
              << " source_frame=110 transaction=committed\n";

    const auto generation_two = baselines(*registry, 2U);
    if (!generation_two || !state.reset_source_generation(
            2U, 1U, registry, generation_two)) {
        return failed("generation_reset");
    }
    const auto old_base = decode(
        decoder, state, kOldGenerationBase, 111U, 8U);
    if (old_base || !old_base.error || !old_base.error->clientdata_error ||
        *old_base.error->clientdata_error !=
            goldsrc::ClientDataDecodeErrorCode::missing_delta_base ||
        state.client_data_state().history().frame_count() != 0U) {
        return failed("generation_isolation");
    }
    std::cout << "scenario=generation_reset typed="
              << goldsrc::to_string(*old_base.error->clientdata_error)
              << " old_history=unavailable\n";
    std::cout << "stock_verification="
              << goldsrc::to_string(mixed_event->stock_verification)
              << '\n';
    std::cout << "result=success\n";
    return 0;
}
