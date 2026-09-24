#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/packet_entity_decoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

// Project-owned literal svc_deltadescription for schema "x": one unsigned
// 8-bit byte field. The array is input to the production schema parser.
constexpr std::array kSchemaBytes{
    std::byte{0x0e}, std::byte{0x78}, std::byte{0x00}, std::byte{0x01},
    std::byte{0x00}, std::byte{0xd9}, std::byte{0x0b}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xc8}, std::byte{0x03},
    std::byte{0x08}, std::byte{0x40}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

// Five identity baselines, full 0xffff terminator, one instanced baseline,
// then the six-bit count/body and zero byte padding. Values are 10,20,30,
// 50,60; the instanced value is 70.
constexpr std::array kBaselineBytes{
    std::byte{0x01}, std::byte{0x20}, std::byte{0x01}, std::byte{0x0a},
    std::byte{0x0a}, std::byte{0x20}, std::byte{0x01}, std::byte{0x14},
    std::byte{0x14}, std::byte{0x20}, std::byte{0x01}, std::byte{0x1e},
    std::byte{0x28}, std::byte{0x30}, std::byte{0x01}, std::byte{0x32},
    std::byte{0x32}, std::byte{0x20}, std::byte{0x01}, std::byte{0x3c},
    std::byte{0xff}, std::byte{0xff}, std::byte{0x41}, std::byte{0x02},
    std::byte{0x8c}, std::byte{0x00}};

// svc_time(100), svc_packetentities(count=5), records using sequential,
// relative and absolute entity-number branches plus identity, intra-message,
// instanced and custom baselines, the 16-bit zero terminator, and svc_setview.
constexpr std::array kFullFrameBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x28}, std::byte{0x05}, std::byte{0x00},
    std::byte{0x91}, std::byte{0x80}, std::byte{0x05}, std::byte{0x12},
    std::byte{0x24}, std::byte{0xc0}, std::byte{0x42}, std::byte{0x0a},
    std::byte{0x30}, std::byte{0x48}, std::byte{0x40}, std::byte{0x08},
    std::byte{0x8a}, std::byte{0x40}, std::byte{0x02}, std::byte{0x96},
    std::byte{0x50}, std::byte{0x92}, std::byte{0x00}, std::byte{0x16},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, std::byte{0x01},
    std::byte{0x00}};

// svc_time(101), delta tag 250: update entity 1, remove entity 20, add
// entity 35 from instanced baseline 0, omit/preserve entities 10,30,40.
constexpr std::array kDeltaFrameBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xca},
    std::byte{0x42}, std::byte{0x29}, std::byte{0x05}, std::byte{0x00},
    std::byte{0xfa}, std::byte{0x04}, std::byte{0x24}, std::byte{0x80},
    std::byte{0xa1}, std::byte{0x89}, std::byte{0x47}, std::byte{0x20},
    std::byte{0x01}, std::byte{0x50}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x01}};

// Source frame 253 deliberately refers to older committed frame 250, not
// latest frame 251. It updates entity 10 and reconstructs count five.
constexpr std::array kOlderBaseDeltaBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xcc},
    std::byte{0x42}, std::byte{0x29}, std::byte{0x05}, std::byte{0x00},
    std::byte{0xfa}, std::byte{0x28}, std::byte{0x24}, std::byte{0xe0},
    std::byte{0x02}, std::byte{0x00}, std::byte{0x00}};

constexpr std::array kMissingBaseBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xce},
    std::byte{0x42}, std::byte{0x29}, std::byte{0x05}, std::byte{0x00},
    std::byte{0xfc}};

constexpr std::array kRecoveryFullBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xd0},
    std::byte{0x42}, std::byte{0x28}, std::byte{0x01}, std::byte{0x00},
    std::byte{0xc8}, std::byte{0x48}, std::byte{0x40}, std::byte{0x0f},
    std::byte{0x00}, std::byte{0x00}};

[[nodiscard]] goldsrc::OwnedServicePayload payload(
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence)
{
    goldsrc::OwnedServicePayload result;
    result.bytes.assign(bytes.begin(), bytes.end());
    result.source_sequence = sequence;
    result.source_acknowledgement = 17U;
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
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(kSchemaBytes, 0U);
    if (!parsed) {
        return {};
    }
    goldsrc::DeltaSchemaRegistryBuilder builder;
    if (!builder.insert(*parsed.schema)) {
        return {};
    }
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

[[nodiscard]] std::shared_ptr<const goldsrc::EntityBaselineRegistryState>
baselines(const goldsrc::DeltaSchemaRegistryState& registry,
          const std::uint64_t generation)
{
    auto source = payload(kBaselineBytes, 17U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    if (!cursor) {
        return {};
    }
    const auto decoded = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        goldsrc::EntityBaselineDecodeInput{
            &source, *cursor, 0U, generation, 1U, "x", "x", "x",
            kBaselineBytes.size() * 8U},
        registry);
    if (!decoded || decoded.entity_count != 5U ||
        decoded.instanced_count != 1U ||
        decoded.bits_consumed != kBaselineBytes.size() * 8U) {
        return {};
    }
    return std::make_shared<const goldsrc::EntityBaselineRegistryState>(
        std::move(*decoded.registry));
}

[[nodiscard]] goldsrc::PacketEntityDecodeResult decode(
    const goldsrc::GoldSrcPacketEntityDecoder& decoder,
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence,
    const std::size_t ordinal,
    goldsrc::PacketEntitySnapshotState& state)
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

[[nodiscard]] const goldsrc::PacketEntityMessageEvent* entity_event(
    const goldsrc::PacketEntityDecodeResult& result)
{
    if (!result || !result.batch) {
        return nullptr;
    }
    for (const auto& event : result.batch->events) {
        if (const auto* entity =
                std::get_if<goldsrc::PacketEntityMessageEvent>(&event)) {
            return entity;
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint32_t y_value(
    const goldsrc::EntitySnapshotState& snapshot,
    const std::uint32_t entity_number)
{
    const auto* entity = snapshot.find_exact(entity_number);
    if (entity == nullptr) {
        return 0U;
    }
    const auto* field = entity->object().find_exact("y");
    if (field == nullptr ||
        !std::holds_alternative<std::uint32_t>(field->value())) {
        return 0U;
    }
    return std::get<std::uint32_t>(field->value());
}

bool print_success(const std::string_view label,
                   const goldsrc::PacketEntityDecodeResult& result)
{
    const auto* event = entity_event(result);
    if (event == nullptr || !event->snapshot || !result.batch) {
        if (result.error) {
            std::cout << "step=" << label
                      << " error=" << goldsrc::to_string(result.error->code)
                      << " context=" << result.error->context << '\n';
        }
        return false;
    }
    const auto& statistics = event->snapshot->statistics();
    std::cout << "step=" << label
              << " kind=" << goldsrc::to_string(event->kind)
              << " resolved_base=";
    if (event->resolved_base) {
        std::cout << event->resolved_base->source_transport_sequence;
    } else {
        std::cout << "none";
    }
    std::cout << " entities=" << event->snapshot->entity_count()
              << " records=" << event->wire_record_count
              << " added=" << statistics.added_count
              << " updated=" << statistics.changed_count
              << " unchanged=" << statistics.unchanged_shared_count
              << " removed=" << statistics.removed_count
              << " y1=" << y_value(*event->snapshot, 1U)
              << " y10=" << y_value(*event->snapshot, 10U)
              << " y35=" << y_value(*event->snapshot, 35U)
              << " cursor_bits="
              << result.batch->end_cursor.absolute_bit_offset() << '\n';
    return true;
}

} // namespace

int main()
{
    const auto schema_registry = schemas();
    const auto baseline_registry =
        schema_registry ? baselines(*schema_registry, 1U) : nullptr;
    if (!schema_registry || !baseline_registry) {
        std::cout << "result=failure stage=baseline_decode\n";
        return 1;
    }

    const auto* fixture_schema = schema_registry->find_exact("x");
    if (fixture_schema == nullptr || fixture_schema->field_count() != 1U) {
        std::cout << "result=failure stage=schema_shape\n";
        return 1;
    }
    std::cout << "schema_field=" << fixture_schema->fields().front().name()
              << " signed="
              << (fixture_schema->fields().front().type_flags().signed_value()
                      ? "yes"
                      : "no")
              << " base="
              << goldsrc::to_string(
                     fixture_schema->fields().front().type_flags().base_type())
              << '\n';

    std::cout << "profile="
              << goldsrc::to_string(
                     goldsrc::PacketEntityCompatibilityProfile::
                         public_goldsrc48_packet_entities_v1)
              << " source="
              << goldsrc::to_string(
                     goldsrc::PacketEntitySpecificationSource::
                         public_protocol_reference)
              << " stock_verification="
              << goldsrc::to_string(
                     goldsrc::PacketEntityStockVerification::
                         not_verified_against_stock_runtime_payload)
              << " baselines=" << baseline_registry->baseline_count() << '\n';

    const goldsrc::GoldSrcPacketEntityDecoder decoder;
    goldsrc::PacketEntitySnapshotState state{
        1U, 1U, schema_registry, baseline_registry};
    const auto full = decode(decoder, kFullFrameBytes, 250U, 1U, state);
    if (!print_success("full", full) ||
        state.history().snapshot_count() != 1U ||
        !state.current_snapshot() ||
        state.current_snapshot()->entity_count() != 5U) {
        std::cout << "result=failure stage=full\n";
        return 1;
    }

    const auto delta = decode(decoder, kDeltaFrameBytes, 251U, 2U, state);
    if (!print_success("delta", delta) || !state.current_snapshot() ||
        y_value(*state.current_snapshot(), 1U) != 12U ||
        state.current_snapshot()->find_exact(20U) != nullptr ||
        y_value(*state.current_snapshot(), 35U) != 80U) {
        std::cout << "result=failure stage=delta\n";
        return 1;
    }

    const auto older =
        decode(decoder, kOlderBaseDeltaBytes, 253U, 3U, state);
    if (!print_success("older_base_delta", older) ||
        !state.current_snapshot() ||
        y_value(*state.current_snapshot(), 1U) != 11U ||
        y_value(*state.current_snapshot(), 10U) != 23U ||
        state.current_snapshot()->find_exact(20U) == nullptr ||
        state.current_snapshot()->find_exact(35U) != nullptr) {
        std::cout << "result=failure stage=older_base_delta\n";
        return 1;
    }

    const auto before_missing = state.current_snapshot();
    const auto history_before_missing = state.history().snapshot_count();
    const auto missing =
        decode(decoder, kMissingBaseBytes, 254U, 4U, state);
    if (missing || !missing.error ||
        missing.error->code !=
            goldsrc::PacketEntityDecodeErrorCode::missing_delta_base ||
        missing.error->recovery !=
            goldsrc::PacketEntityRecoveryStatus::full_snapshot_required ||
        state.current_snapshot() != before_missing ||
        state.history().snapshot_count() != history_before_missing) {
        std::cout << "result=failure stage=missing_base\n";
        return 1;
    }
    std::cout << "step=missing_base error="
              << goldsrc::to_string(missing.error->code)
              << " recovery=" << goldsrc::to_string(missing.error->recovery)
              << " state_unchanged=yes\n";

    const auto recovery =
        decode(decoder, kRecoveryFullBytes, 255U, 5U, state);
    if (!print_success("recovery_full", recovery) ||
        !state.current_snapshot() ||
        state.current_snapshot()->entity_count() != 1U ||
        y_value(*state.current_snapshot(), 50U) != 61U) {
        std::cout << "result=failure stage=recovery_full\n";
        return 1;
    }

    const auto generation_two_baselines = baselines(*schema_registry, 2U);
    if (!generation_two_baselines ||
        !state.reset_source_generation(
            2U, 1U, schema_registry, generation_two_baselines)) {
        std::cout << "result=failure stage=generation_reset\n";
        return 1;
    }
    constexpr std::array generation_delta{
        std::byte{0x29}, std::byte{0x01}, std::byte{0x00}, std::byte{0xff}};
    const auto cross_generation =
        decode(decoder, generation_delta, 256U, 6U, state);
    if (cross_generation || !cross_generation.error ||
        cross_generation.error->code !=
            goldsrc::PacketEntityDecodeErrorCode::missing_delta_base ||
        state.current_snapshot() || state.history().snapshot_count() != 0U) {
        std::cout << "result=failure stage=cross_generation\n";
        return 1;
    }
    std::cout << "step=generation_reset error="
              << goldsrc::to_string(cross_generation.error->code)
              << " recovery="
              << goldsrc::to_string(cross_generation.error->recovery)
              << " history=0 state_unchanged=yes\n"
              << "result=success\n";
    return 0;
}
