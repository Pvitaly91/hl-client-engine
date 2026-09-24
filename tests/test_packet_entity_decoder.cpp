#include <hlclient/goldsrc/packet_entity_decoder.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>

#include "delta_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace fixture = hlclient::test::delta_fixture;

constexpr std::array kSchemaBytes{
    std::byte{0x0e}, std::byte{0x78}, std::byte{0x00}, std::byte{0x01},
    std::byte{0x00}, std::byte{0xd9}, std::byte{0x0b}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xc8}, std::byte{0x03},
    std::byte{0x08}, std::byte{0x40}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

constexpr std::array kBaselineBytes{
    std::byte{0x01}, std::byte{0x20}, std::byte{0x01}, std::byte{0x0a},
    std::byte{0x0a}, std::byte{0x20}, std::byte{0x01}, std::byte{0x14},
    std::byte{0x14}, std::byte{0x20}, std::byte{0x01}, std::byte{0x1e},
    std::byte{0x28}, std::byte{0x30}, std::byte{0x01}, std::byte{0x32},
    std::byte{0x32}, std::byte{0x20}, std::byte{0x01}, std::byte{0x3c},
    std::byte{0xff}, std::byte{0xff}, std::byte{0x41}, std::byte{0x02},
    std::byte{0x8c}, std::byte{0x00}};

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

constexpr std::array kDeltaFrameBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xca},
    std::byte{0x42}, std::byte{0x29}, std::byte{0x05}, std::byte{0x00},
    std::byte{0xfa}, std::byte{0x04}, std::byte{0x24}, std::byte{0x80},
    std::byte{0xa1}, std::byte{0x89}, std::byte{0x47}, std::byte{0x20},
    std::byte{0x01}, std::byte{0x50}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x01}};

constexpr std::array kOlderBaseDeltaBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xcc},
    std::byte{0x42}, std::byte{0x29}, std::byte{0x05}, std::byte{0x00},
    std::byte{0xfa}, std::byte{0x28}, std::byte{0x24}, std::byte{0xe0},
    std::byte{0x02}, std::byte{0x00}, std::byte{0x00}};

constexpr std::array kRecoveryFullBytes{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xd0},
    std::byte{0x42}, std::byte{0x28}, std::byte{0x01}, std::byte{0x00},
    std::byte{0xc8}, std::byte{0x48}, std::byte{0x40}, std::byte{0x0f},
    std::byte{0x00}, std::byte{0x00}};

[[nodiscard]] std::shared_ptr<const goldsrc::DeltaSchemaRegistryState>
make_schemas()
{
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(kSchemaBytes, 0U);
    REQUIRE(parsed);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

[[nodiscard]] goldsrc::OwnedServicePayload owning_payload(
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence)
{
    auto result = fixture::owning_payload(
        std::vector<std::byte>{bytes.begin(), bytes.end()});
    result.source_sequence = sequence;
    return result;
}

[[nodiscard]] std::shared_ptr<const goldsrc::EntityBaselineRegistryState>
make_baselines(const goldsrc::DeltaSchemaRegistryState& schemas,
               const std::uint64_t generation = 1U)
{
    auto payload = owning_payload(kBaselineBytes, 17U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    const auto result = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        goldsrc::EntityBaselineDecodeInput{
            &payload, *cursor, 0U, generation, 1U, "x", "x", "x",
            kBaselineBytes.size() * 8U},
        schemas);
    REQUIRE(result);
    REQUIRE(result.registry);
    REQUIRE(result.entity_count == 5U);
    REQUIRE(result.instanced_count == 1U);
    return std::make_shared<const goldsrc::EntityBaselineRegistryState>(
        std::move(*result.registry));
}

struct PacketFixture final {
    explicit PacketFixture(goldsrc::PacketEntityDecodeLimits selected = {})
        : limits{selected},
          schemas{make_schemas()},
          baselines{make_baselines(*schemas)},
          state{1U, 1U, schemas, baselines, limits.snapshots},
          decoder{limits}
    {
        REQUIRE(state.valid());
        REQUIRE(decoder.valid_configuration());
    }

    goldsrc::PacketEntityDecodeLimits limits;
    std::shared_ptr<const goldsrc::DeltaSchemaRegistryState> schemas;
    std::shared_ptr<const goldsrc::EntityBaselineRegistryState> baselines;
    goldsrc::PacketEntitySnapshotState state;
    goldsrc::GoldSrcPacketEntityDecoder decoder;
};

[[nodiscard]] goldsrc::PacketEntityDecodeResult decode(
    PacketFixture& fixture_state,
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence,
    const std::size_t ordinal = 1U,
    const std::string_view schema_name = "x")
{
    auto payload = owning_payload(bytes, sequence);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    return fixture_state.decoder.decode_and_apply(
        goldsrc::PacketEntityDecodeInput{
            payload, *cursor, fixture_state.state.source_generation(), ordinal,
            schema_name, schema_name, schema_name},
        fixture_state.state);
}

[[nodiscard]] const goldsrc::PacketEntityMessageEvent& entity_event(
    const goldsrc::PacketEntityDecodeResult& result)
{
    REQUIRE(result);
    REQUIRE(result.batch);
    const goldsrc::PacketEntityMessageEvent* found = nullptr;
    for (const auto& event : result.batch->events) {
        if (const auto* entity =
                std::get_if<goldsrc::PacketEntityMessageEvent>(&event)) {
            found = entity;
            break;
        }
    }
    REQUIRE(found != nullptr);
    return *found;
}

[[nodiscard]] std::uint32_t byte_value(
    const goldsrc::EntitySnapshotState& snapshot,
    const std::uint32_t number)
{
    const auto* entity = snapshot.find_exact(number);
    REQUIRE(entity != nullptr);
    const auto* field = entity->object().find_exact("y");
    REQUIRE(field != nullptr);
    REQUIRE(std::holds_alternative<std::uint32_t>(field->value()));
    return std::get<std::uint32_t>(field->value());
}

void require_error_unchanged(
    PacketFixture& fixture_state,
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence,
    const goldsrc::PacketEntityDecodeErrorCode code,
    const goldsrc::PacketEntityRecoveryStatus recovery =
        goldsrc::PacketEntityRecoveryStatus::none,
    const std::string_view schema_name = "x")
{
    const auto snapshot = fixture_state.state.current_snapshot();
    const auto history_count = fixture_state.state.history().snapshot_count();
    const auto history_bytes =
        fixture_state.state.history().accounted_value_bytes();
    const auto controls = fixture_state.state.control_state();
    const auto result = decode(fixture_state, bytes, sequence, 1U, schema_name);
    const auto context = result.error ? result.error->context : std::string{};
    INFO(context);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == code);
    CHECK(result.error->recovery == recovery);
    CHECK(fixture_state.state.current_snapshot() == snapshot);
    CHECK(fixture_state.state.history().snapshot_count() == history_count);
    CHECK(fixture_state.state.history().accounted_value_bytes() == history_bytes);
    CHECK(fixture_state.state.control_state() == controls);
}

void prefix(fixture::BitWriter& writer,
            const float time,
            const std::uint8_t opcode,
            const std::uint16_t count,
            const std::optional<std::uint8_t> tag = std::nullopt)
{
    writer.write(7U, 8U);
    writer.write(std::bit_cast<std::uint32_t>(time), 32U);
    writer.write(opcode, 8U);
    writer.write(count, 16U);
    if (tag) {
        writer.write(*tag, 8U);
    }
}

void byte_delta(fixture::BitWriter& writer, const std::uint8_t value)
{
    writer.write(1U, 3U);
    writer.write(1U, 8U);
    writer.write(value, 8U);
}

void finish_entities(fixture::BitWriter& writer)
{
    writer.write(goldsrc::kGoldSrcPacketEntityTerminator,
                 goldsrc::kGoldSrcPacketEntityTerminatorBits);
    writer.align_zero();
}

[[nodiscard]] std::vector<std::byte> zero_delta(
    const std::uint16_t final_count,
    const std::uint8_t base_tag,
    const float time = 102.0F)
{
    fixture::BitWriter writer;
    prefix(writer, time, goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode,
           final_count, base_tag);
    finish_entities(writer);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> empty_full(const float time = 1.0F)
{
    fixture::BitWriter writer;
    prefix(writer, time, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 0U);
    finish_entities(writer);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> malformed_full_numbers(
    const bool duplicate)
{
    fixture::BitWriter writer;
    prefix(writer, 1.0F, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 2U);
    writer.write(0U, 1U);
    writer.write(1U, 1U);
    writer.write(duplicate ? 10U : 20U, 11U);
    writer.write(0U, 1U); // ordinary
    writer.write(0U, 1U); // not instanced
    writer.write(0U, 1U); // no intra-message base
    byte_delta(writer, 21U);
    writer.write(0U, 1U);
    writer.write(1U, 1U);
    writer.write(10U, 11U);
    // No body is required: ordering validation occurs at this point.
    writer.write(1U, 16U);
    writer.align_zero();
    return writer.bytes();
}

} // namespace

TEST_CASE("Reference packet-entity profile is explicit and executable",
          "[goldsrc][packet-entities][profile]")
{
    const goldsrc::GoldSrcPacketEntityDecoder decoder;
    CHECK(decoder.valid_configuration());
    CHECK(decoder.profile() ==
          goldsrc::PacketEntityCompatibilityProfile::
              public_goldsrc48_packet_entities_v1);
    CHECK(goldsrc::to_string(decoder.profile()) ==
          "public_goldsrc48_packet_entities_v1");
    CHECK(goldsrc::kGoldSrcSvcPacketEntitiesOpcode == 40U);
    CHECK(goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode == 41U);
    CHECK(goldsrc::kGoldSrcPacketEntityCountBits == 16U);
    CHECK(goldsrc::kGoldSrcPacketEntityDeltaBaseTagBits == 8U);
    CHECK(goldsrc::kGoldSrcPacketEntityTerminatorBits == 16U);
    CHECK(goldsrc::to_string(
              goldsrc::PacketEntitySpecificationSource::
                  public_protocol_reference) == "public_protocol_reference");
    CHECK(goldsrc::to_string(
              goldsrc::PacketEntityStockVerification::
                  not_verified_against_stock_runtime_payload) ==
          "not_verified_against_stock_runtime_payload");

    PacketFixture fixture_state;
    const goldsrc::GoldSrcPacketEntityDecoder unknown{
        {}, static_cast<goldsrc::PacketEntityCompatibilityProfile>(0xffU)};
    CHECK_FALSE(unknown.valid_configuration());
    auto payload = owning_payload(kFullFrameBytes, 250U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    const auto invalid = unknown.decode_and_apply(
        goldsrc::PacketEntityDecodeInput{
            payload, *cursor, 1U, 1U, "x", "x", "x"},
        fixture_state.state);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code ==
          goldsrc::PacketEntityDecodeErrorCode::invalid_profile);
}

TEST_CASE("Literal full packet entities owns exact decoded state and cursor",
          "[goldsrc][packet-entities][full][literal]")
{
    PacketFixture fixture_state;
    const auto result = decode(fixture_state, kFullFrameBytes, 250U, 7U);
    const auto& event = entity_event(result);
    REQUIRE(result.batch);
    REQUIRE(event.snapshot);

    CHECK(event.kind == goldsrc::PacketEntityMessageKind::full);
    CHECK(event.wire_entity_count == 5U);
    CHECK(event.wire_record_count == 5U);
    CHECK_FALSE(event.wire_delta_base_tag);
    CHECK_FALSE(event.resolved_base);
    CHECK(event.start_cursor.absolute_bit_offset() == 40U);
    CHECK(event.end_cursor.absolute_bit_offset() == 240U);
    CHECK(result.batch->end_cursor.absolute_bit_offset() ==
          kFullFrameBytes.size() * 8U);
    CHECK(result.batch->consumed_bit_count == kFullFrameBytes.size() * 8U);
    CHECK(result.batch->events.size() == 3U);
    CHECK(result.batch->specification_source ==
          goldsrc::PacketEntitySpecificationSource::
              public_protocol_reference);
    CHECK(result.batch->stock_verification ==
          goldsrc::PacketEntityStockVerification::
              not_verified_against_stock_runtime_payload);

    const auto& snapshot = *event.snapshot;
    CHECK(snapshot.reference().policy() ==
          goldsrc::EntitySnapshotReferencePolicy::
              goldsrc_transport_sequence_30bit);
    CHECK(snapshot.reference().value() == 250U);
    CHECK(snapshot.kind() == goldsrc::EntitySnapshotKind::full);
    CHECK_FALSE(snapshot.base_reference());
    CHECK(snapshot.entity_count() == 5U);
    CHECK(snapshot.statistics().entity_count == 5U);
    CHECK(snapshot.statistics().added_count == 5U);
    CHECK(snapshot.statistics().changed_count == 0U);
    CHECK(byte_value(snapshot, 1U) == 11U);
    CHECK(byte_value(snapshot, 10U) == 22U);
    CHECK(byte_value(snapshot, 20U) == 33U);
    CHECK(byte_value(snapshot, 30U) == 75U);
    CHECK(byte_value(snapshot, 40U) == 44U);
    REQUIRE(snapshot.server_time().goldsrc_seconds());
    CHECK(*snapshot.server_time().goldsrc_seconds() == 100.0);

    REQUIRE(snapshot.find_exact(1U));
    REQUIRE(snapshot.find_exact(10U));
    REQUIRE(snapshot.find_exact(20U));
    REQUIRE(snapshot.find_exact(30U));
    REQUIRE(snapshot.find_exact(40U));
    CHECK(snapshot.find_exact(1U)->schema_category() ==
          goldsrc::EntitySchemaCategory::player_entity);
    CHECK(snapshot.find_exact(10U)->schema_category() ==
          goldsrc::EntitySchemaCategory::ordinary_entity);
    CHECK(snapshot.find_exact(40U)->schema_category() ==
          goldsrc::EntitySchemaCategory::custom_entity);
    CHECK(snapshot.find_exact(1U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::entity_baseline);
    CHECK(snapshot.find_exact(20U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::intra_message_entity);
    CHECK(snapshot.find_exact(20U)->state_base_reference().value() == 10U);
    CHECK(snapshot.find_exact(30U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::instanced_baseline);
    CHECK(snapshot.find_exact(30U)->state_base_reference().value() == 0U);

    REQUIRE(fixture_state.state.control_state().server_time());
    REQUIRE(fixture_state.state.control_state().view_entity());
    CHECK(fixture_state.state.control_state().server_time()->seconds == 100.0F);
    CHECK(fixture_state.state.control_state().view_entity()
              ->wire_entity_reference == 1);
    CHECK(fixture_state.state.history().snapshot_count() == 1U);
    REQUIRE(fixture_state.state.history().source_generation());
    CHECK(*fixture_state.state.history().source_generation() == 1U);
}

TEST_CASE("Literal delta updates adds removes and preserves omissions",
          "[goldsrc][packet-entities][delta][literal]")
{
    PacketFixture fixture_state;
    const auto full = decode(fixture_state, kFullFrameBytes, 250U);
    const auto& base_event = entity_event(full);
    REQUIRE(base_event.snapshot);
    const auto* base_ten = base_event.snapshot->find_exact(10U);
    REQUIRE(base_ten != nullptr);

    const auto result = decode(fixture_state, kDeltaFrameBytes, 251U);
    const auto& event = entity_event(result);
    REQUIRE(event.snapshot);
    CHECK(event.kind == goldsrc::PacketEntityMessageKind::delta);
    CHECK(event.wire_entity_count == 5U);
    CHECK(event.wire_record_count == 3U);
    REQUIRE(event.wire_delta_base_tag);
    CHECK(event.wire_delta_base_tag->value == 250U);
    REQUIRE(event.resolved_base);
    CHECK(event.resolved_base->source_transport_sequence == 250U);
    REQUIRE(event.snapshot->base_reference());
    CHECK(event.snapshot->base_reference()->value() == 250U);
    CHECK(event.snapshot->entity_count() == 5U);
    CHECK(event.snapshot->statistics().added_count == 1U);
    CHECK(event.snapshot->statistics().changed_count == 1U);
    CHECK(event.snapshot->statistics().removed_count == 1U);
    CHECK(event.snapshot->statistics().unchanged_shared_count == 3U);
    CHECK(byte_value(*event.snapshot, 1U) == 12U);
    CHECK(byte_value(*event.snapshot, 10U) == 22U);
    CHECK(event.snapshot->find_exact(20U) == nullptr);
    CHECK(byte_value(*event.snapshot, 35U) == 80U);
    CHECK(byte_value(*event.snapshot, 40U) == 44U);
    REQUIRE(event.snapshot->removed_entity_numbers().size() == 1U);
    CHECK(event.snapshot->removed_entity_numbers().front() == 20U);
    REQUIRE(event.snapshot->find_exact(10U));
    CHECK(&event.snapshot->find_exact(10U)->object() == &base_ten->object());
    CHECK(event.snapshot->find_exact(1U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::previous_snapshot_entity);
    CHECK(event.snapshot->find_exact(35U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::instanced_baseline);
}

TEST_CASE("Delta resolves its named older frame rather than latest history",
          "[goldsrc][packet-entities][history][older-base]")
{
    PacketFixture fixture_state;
    REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
    REQUIRE(decode(fixture_state, kDeltaFrameBytes, 251U));
    const auto result = decode(fixture_state, kOlderBaseDeltaBytes, 253U);
    const auto& event = entity_event(result);
    REQUIRE(event.snapshot);
    REQUIRE(event.resolved_base);
    CHECK(event.resolved_base->source_transport_sequence == 250U);
    CHECK(byte_value(*event.snapshot, 1U) == 11U);
    CHECK(byte_value(*event.snapshot, 10U) == 23U);
    CHECK(event.snapshot->find_exact(20U) != nullptr);
    CHECK(event.snapshot->find_exact(35U) == nullptr);
    CHECK(fixture_state.state.history().snapshot_count() == 3U);
}

TEST_CASE("Delta absolute-number branch can add from an exact identity baseline",
          "[goldsrc][packet-entities][delta][absolute][baseline]")
{
    PacketFixture fixture_state;
    REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));

    fixture::BitWriter writer;
    prefix(writer, 101.0F,
           goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode, 6U, 250U);
    writer.write(0U, 1U);  // non-removal
    writer.write(1U, 1U);  // absolute number follows
    writer.write(50U, 11U);
    writer.write(0U, 1U);  // ordinary schema
    writer.write(0U, 1U);  // not instanced: identity baseline 50
    byte_delta(writer, 63U);
    finish_entities(writer);

    const auto result = decode(fixture_state, writer.bytes(), 251U);
    const auto& event = entity_event(result);
    REQUIRE(event.snapshot);
    CHECK(event.wire_record_count == 1U);
    CHECK(event.snapshot->entity_count() == 6U);
    CHECK(event.snapshot->statistics().added_count == 1U);
    CHECK(event.snapshot->statistics().unchanged_shared_count == 5U);
    CHECK(byte_value(*event.snapshot, 50U) == 63U);
    REQUIRE(event.snapshot->find_exact(50U));
    CHECK(event.snapshot->find_exact(50U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::entity_baseline);
    CHECK(event.snapshot->find_exact(50U)->state_base_reference().value() ==
          50U);
}

TEST_CASE("Zero-record delta preserves base and full replaces prior set",
          "[goldsrc][packet-entities][zero-change][full-replace]")
{
    PacketFixture fixture_state;
    REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
    const auto zero = zero_delta(5U, 250U);
    const auto delta = decode(fixture_state, zero, 252U);
    const auto& delta_event = entity_event(delta);
    REQUIRE(delta_event.snapshot);
    CHECK(delta_event.wire_record_count == 0U);
    CHECK(delta_event.snapshot->entity_count() == 5U);
    CHECK(delta_event.snapshot->statistics().unchanged_shared_count == 5U);
    CHECK(delta_event.snapshot->statistics().added_count == 0U);
    CHECK(delta_event.snapshot->statistics().changed_count == 0U);

    const auto full = decode(fixture_state, kRecoveryFullBytes, 255U);
    const auto& full_event = entity_event(full);
    REQUIRE(full_event.snapshot);
    CHECK(full_event.snapshot->entity_count() == 1U);
    CHECK(full_event.snapshot->find_exact(50U) != nullptr);
    CHECK(full_event.snapshot->find_exact(1U) == nullptr);
    CHECK(byte_value(*full_event.snapshot, 50U) == 61U);

    const auto empty = empty_full(105.0F);
    const auto empty_result = decode(fixture_state, empty, 256U);
    const auto& empty_event = entity_event(empty_result);
    REQUIRE(empty_event.snapshot);
    CHECK(empty_event.wire_entity_count == 0U);
    CHECK(empty_event.wire_record_count == 0U);
    CHECK(empty_event.snapshot->entity_count() == 0U);
}

TEST_CASE("Missing and evicted exact bases request full snapshot atomically",
          "[goldsrc][packet-entities][history][recovery]")
{
    SECTION("missing exact base and same low bits in another epoch")
    {
        PacketFixture fixture_state;
        REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
        const auto missing = zero_delta(5U, 250U, 103.0F);
        require_error_unchanged(
            fixture_state, missing, 513U,
            goldsrc::PacketEntityDecodeErrorCode::missing_delta_base,
            goldsrc::PacketEntityRecoveryStatus::full_snapshot_required);
    }

    SECTION("evicted base")
    {
        goldsrc::PacketEntityDecodeLimits limits;
        limits.snapshots.maximum_snapshot_history = 2U;
        PacketFixture fixture_state{limits};
        REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
        REQUIRE(decode(fixture_state, zero_delta(5U, 250U), 251U));
        REQUIRE(decode(fixture_state, zero_delta(5U, 251U), 252U));
        CHECK(fixture_state.state.history().snapshot_count() == 2U);
        REQUIRE(fixture_state.state.history().evicted_through());
        CHECK(fixture_state.state.history().evicted_through()->value() == 250U);
        require_error_unchanged(
            fixture_state, zero_delta(5U, 250U), 253U,
            goldsrc::PacketEntityDecodeErrorCode::evicted_delta_base,
            goldsrc::PacketEntityRecoveryStatus::full_snapshot_required);
    }
}

TEST_CASE("Packet entity sequence tags wrap in the 30-bit transport domain",
          "[goldsrc][packet-entities][history][wrap]")
{
    PacketFixture fixture_state;
    constexpr auto near_wrap = goldsrc::kNetchanSequenceMask - 1U;
    REQUIRE(decode(fixture_state, kFullFrameBytes, near_wrap));
    const auto wrapped = zero_delta(5U, 0xfeU, 103.0F);
    const auto result = decode(fixture_state, wrapped, 1U);
    const auto& event = entity_event(result);
    REQUIRE(event.resolved_base);
    CHECK(event.resolved_base->source_transport_sequence == near_wrap);
    REQUIRE(event.snapshot);
    CHECK(event.snapshot->reference().value() == 1U);
    CHECK(event.snapshot->entity_count() == 5U);

    require_error_unchanged(
        fixture_state, zero_delta(5U, 2U), 2U,
        goldsrc::PacketEntityDecodeErrorCode::invalid_delta_base_lookback,
        goldsrc::PacketEntityRecoveryStatus::full_snapshot_required);
    require_error_unchanged(
        fixture_state, zero_delta(5U, 195U), 2U,
        goldsrc::PacketEntityDecodeErrorCode::invalid_delta_base_lookback,
        goldsrc::PacketEntityRecoveryStatus::full_snapshot_required);
}

TEST_CASE("Duplicate conflicting and old source frames are typed",
          "[goldsrc][packet-entities][reorder][transactional]")
{
    PacketFixture fixture_state;
    REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
    require_error_unchanged(
        fixture_state, kFullFrameBytes, 250U,
        goldsrc::PacketEntityDecodeErrorCode::duplicate_source_frame);

    auto conflict = std::vector<std::byte>{
        kFullFrameBytes.begin(), kFullFrameBytes.end()};
    conflict[1] = std::byte{0x01};
    require_error_unchanged(
        fixture_state, conflict, 250U,
        goldsrc::PacketEntityDecodeErrorCode::conflicting_source_frame);

    REQUIRE(decode(fixture_state, kDeltaFrameBytes, 251U));
    require_error_unchanged(
        fixture_state, kRecoveryFullBytes, 249U,
        goldsrc::PacketEntityDecodeErrorCode::old_source_frame);
}

TEST_CASE("Malformed record order baseline choice count and padding are rejected",
          "[goldsrc][packet-entities][negative][transactional]")
{
    SECTION("duplicate and descending numbers")
    {
        PacketFixture duplicate;
        require_error_unchanged(
            duplicate, malformed_full_numbers(true), 20U,
            goldsrc::PacketEntityDecodeErrorCode::duplicate_entity_record);
        PacketFixture descending;
        require_error_unchanged(
            descending, malformed_full_numbers(false), 20U,
            goldsrc::PacketEntityDecodeErrorCode::out_of_order_entity_record);
    }

    SECTION("header is final reconstructed count, not wire records")
    {
        PacketFixture fixture_state;
        auto wrong_count = std::vector<std::byte>{
            kFullFrameBytes.begin(), kFullFrameBytes.end()};
        wrong_count[6] = std::byte{0x04};
        require_error_unchanged(
            fixture_state, wrong_count, 250U,
            goldsrc::PacketEntityDecodeErrorCode::invalid_entity_count);
    }

    SECTION("padding after terminator must be zero")
    {
        PacketFixture fixture_state;
        auto padding = std::vector<std::byte>{
            kFullFrameBytes.begin(), kFullFrameBytes.end()};
        padding[29] = std::byte{0x80};
        require_error_unchanged(
            fixture_state, padding, 250U,
            goldsrc::PacketEntityDecodeErrorCode::malformed_padding);
    }

    SECTION("existing entity cannot silently change schema category")
    {
        PacketFixture fixture_state;
        REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
        fixture::BitWriter writer;
        prefix(writer, 101.0F,
               goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode, 5U, 250U);
        writer.write(0U, 1U); // update
        writer.write(0U, 1U); // relative
        writer.write(10U, 6U);
        writer.write(1U, 1U); // custom: incompatible with ordinary base
        writer.write(0U, 1U); // not instanced
        byte_delta(writer, 25U);
        finish_entities(writer);
        require_error_unchanged(
            fixture_state, writer.bytes(), 251U,
            goldsrc::PacketEntityDecodeErrorCode::schema_mismatch);
    }

    SECTION("instanced slot is exact")
    {
        PacketFixture fixture_state;
        fixture::BitWriter writer;
        prefix(writer, 1.0F, goldsrc::kGoldSrcSvcPacketEntitiesOpcode,
               1U);
        writer.write(0U, 1U);
        writer.write(0U, 1U);
        writer.write(35U, 6U);
        writer.write(0U, 1U);
        writer.write(1U, 1U);
        writer.write(2U, 6U); // only slot 0 exists
        byte_delta(writer, 80U);
        finish_entities(writer);
        require_error_unchanged(
            fixture_state, writer.bytes(), 20U,
            goldsrc::PacketEntityDecodeErrorCode::invalid_baseline_reference);
    }

    SECTION("removal must name an entity in the exact base")
    {
        PacketFixture fixture_state;
        REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
        fixture::BitWriter writer;
        prefix(writer, 101.0F,
               goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode, 5U, 250U);
        writer.write(1U, 1U);  // removal
        writer.write(0U, 1U);  // relative number
        writer.write(35U, 6U); // entity 35 is absent from frame 250
        finish_entities(writer);
        require_error_unchanged(
            fixture_state, writer.bytes(), 251U,
            goldsrc::PacketEntityDecodeErrorCode::remove_nonexistent_entity);
    }

    SECTION("zero entity number is not a record")
    {
        PacketFixture fixture_state;
        fixture::BitWriter writer;
        prefix(writer, 1.0F, goldsrc::kGoldSrcSvcPacketEntitiesOpcode,
               1U);
        writer.write(0U, 1U); // not sequential
        writer.write(0U, 1U); // relative number
        writer.write(0U, 6U); // forbidden zero difference => entity zero
        byte_delta(writer, 1U); // keep the 16-bit lookahead nonzero
        finish_entities(writer);
        require_error_unchanged(
            fixture_state, writer.bytes(), 20U,
            goldsrc::PacketEntityDecodeErrorCode::invalid_entity_number);
    }

    SECTION("schema names resolve exactly")
    {
        PacketFixture fixture_state;
        require_error_unchanged(
            fixture_state, kFullFrameBytes, 250U,
            goldsrc::PacketEntityDecodeErrorCode::unknown_schema,
            goldsrc::PacketEntityRecoveryStatus::none, "missing");
    }
}

TEST_CASE("Critical packet entity truncations and unknown suffix preserve state",
          "[goldsrc][packet-entities][truncation][transactional]")
{
    SECTION("headers")
    {
        PacketFixture fixture_state;
        require_error_unchanged(
            fixture_state,
            std::array{std::byte{goldsrc::kGoldSrcSvcPacketEntitiesOpcode}},
            1U, goldsrc::PacketEntityDecodeErrorCode::truncated_header);
        require_error_unchanged(
            fixture_state,
            std::array{std::byte{goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode},
                       std::byte{0x00}, std::byte{0x00}},
            1U, goldsrc::PacketEntityDecodeErrorCode::truncated_header,
            goldsrc::PacketEntityRecoveryStatus::full_snapshot_required);
    }

    SECTION("runtime time context is explicit")
    {
        constexpr std::array without_time{
            std::byte{goldsrc::kGoldSrcSvcPacketEntitiesOpcode},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x00}};
        PacketFixture fixture_state;
        require_error_unchanged(
            fixture_state, without_time, 1U,
            goldsrc::PacketEntityDecodeErrorCode::missing_server_time);
    }

    SECTION("terminator")
    {
        PacketFixture fixture_state;
        auto truncated = empty_full();
        truncated.pop_back();
        require_error_unchanged(
            fixture_state, truncated, 1U,
            goldsrc::PacketEntityDecodeErrorCode::truncated_terminator);
    }

    SECTION("every prefix before a complete entity message fails")
    {
        for (std::size_t size = 0U; size < 30U; ++size) {
            CAPTURE(size);
            PacketFixture fixture_state;
            const std::span prefix_bytes{kFullFrameBytes.data(), size};
            const auto result = decode(fixture_state, prefix_bytes, 250U);
            if (size == 5U) {
                CHECK(result);
            } else {
                CHECK_FALSE(result);
            }
            CHECK_FALSE(fixture_state.state.current_snapshot());
            CHECK(fixture_state.state.history().snapshot_count() == 0U);
        }
    }

    SECTION("valid entity message plus malformed suffix is one transaction")
    {
        PacketFixture fixture_state;
        auto suffix = std::vector<std::byte>{
            kFullFrameBytes.begin(), kFullFrameBytes.end()};
        suffix[30] = std::byte{0xfe};
        require_error_unchanged(
            fixture_state, suffix, 250U,
            goldsrc::PacketEntityDecodeErrorCode::unsupported_opcode);
    }
}

TEST_CASE("Snapshot safety limits fail before committed publication",
          "[goldsrc][packet-entities][limits][transactional]")
{
    SECTION("entity count")
    {
        goldsrc::PacketEntityDecodeLimits limits;
        limits.snapshots.maximum_entities_per_snapshot = 4U;
        PacketFixture fixture_state{limits};
        require_error_unchanged(
            fixture_state, kFullFrameBytes, 250U,
            goldsrc::PacketEntityDecodeErrorCode::entity_limit_exceeded);
    }

    SECTION("aggregate retained values")
    {
        goldsrc::PacketEntityDecodeLimits limits;
        limits.snapshots.maximum_snapshot_total_value_bytes = 1U;
        PacketFixture fixture_state{limits};
        require_error_unchanged(
            fixture_state, kFullFrameBytes, 250U,
            goldsrc::PacketEntityDecodeErrorCode::total_value_bytes_exceeded);
    }
}

TEST_CASE("Generation reset severs current state history and old baseline domain",
          "[goldsrc][packet-entities][generation]")
{
    PacketFixture fixture_state;
    REQUIRE(decode(fixture_state, kFullFrameBytes, 250U));
    const auto generation_two =
        make_baselines(*fixture_state.schemas, 2U);
    REQUIRE(fixture_state.state.reset_source_generation(
        2U, 1U, fixture_state.schemas, generation_two));
    CHECK_FALSE(fixture_state.state.current_snapshot());
    CHECK(fixture_state.state.history().snapshot_count() == 0U);
    CHECK_FALSE(fixture_state.state.control_state().server_time());

    require_error_unchanged(
        fixture_state, zero_delta(5U, 250U), 251U,
        goldsrc::PacketEntityDecodeErrorCode::missing_delta_base,
        goldsrc::PacketEntityRecoveryStatus::full_snapshot_required);
}

TEST_CASE("Packet fields use signed quantized angle and staged time context",
          "[goldsrc][packet-entities][numeric][time-window]")
{
    constexpr fixture::Field fields[]{
        {"origin[0]", 0x8000'0004U, 0U, 8U, 4'000U, 4'000U},
        {"angles[1]", 0x0000'0010U, 4U, 8U, 400U, 4'000U},
        {"animtime", 0x0000'0020U, 8U, 8U, 4'000U, 4'000U},
    };
    const auto schema_bytes = fixture::schema("rich", fields);
    const auto parsed =
        goldsrc::DeltaDescriptionParser{}.parse(schema_bytes, 0U);
    REQUIRE(parsed);
    goldsrc::DeltaSchemaRegistryBuilder schema_builder;
    REQUIRE(schema_builder.insert(*parsed.schema));
    auto schemas = std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(schema_builder).publish());

    fixture::BitWriter baseline_writer;
    baseline_writer.write(2U, 11U);
    baseline_writer.write(0U, 2U);
    baseline_writer.write(0U, 3U); // zero changed fields from null object
    baseline_writer.write(0xffffU, 16U);
    baseline_writer.write(0U, 6U);
    baseline_writer.align_zero();
    auto baseline_payload = owning_payload(baseline_writer.bytes(), 10U);
    const auto baseline_cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, baseline_payload.bytes.size());
    REQUIRE(baseline_cursor);
    const auto baseline_result =
        goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
            goldsrc::EntityBaselineDecodeInput{
                &baseline_payload, *baseline_cursor, 0U, 1U, 1U,
                "rich", "rich", "rich",
                baseline_writer.bit_offset()},
            *schemas);
    REQUIRE(baseline_result);
    auto baselines =
        std::make_shared<const goldsrc::EntityBaselineRegistryState>(
            std::move(*baseline_result.registry));

    goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
    goldsrc::GoldSrcPacketEntityDecoder decoder;
    fixture::BitWriter message;
    prefix(message, 2.0F, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 1U);
    message.write(0U, 1U);
    message.write(0U, 1U);
    message.write(2U, 6U);
    message.write(0U, 1U); // ordinary
    message.write(0U, 1U); // no intra-message base
    message.write(1U, 3U);
    message.write(0x07U, 8U);
    message.write(9U, 8U);    // sign 1, magnitude 4
    message.write(64U, 8U);   // 90 degrees
    message.write(25U, 8U);   // 2.0 - 25/100 = 1.75
    finish_entities(message);
    auto payload = owning_payload(message.bytes(), 20U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    const auto result = decoder.decode_and_apply(
        goldsrc::PacketEntityDecodeInput{
            payload, *cursor, 1U, 1U, "rich", "rich", "rich"},
        state);
    const auto& event = entity_event(result);
    REQUIRE(event.snapshot);
    const auto* entity = event.snapshot->find_exact(2U);
    REQUIRE(entity);
    const auto* origin = entity->object().find_exact("origin[0]");
    const auto* angle = entity->object().find_exact("angles[1]");
    const auto* time = entity->object().find_exact("animtime");
    REQUIRE(origin);
    REQUIRE(angle);
    REQUIRE(time);
    CHECK(std::abs(std::get<double>(origin->value()) + 4.0) < 0.000'001);
    CHECK(std::abs(std::get<double>(angle->value()) - 90.0) < 0.000'001);
    CHECK(std::abs(std::get<double>(time->value()) - 1.75) < 0.000'001);
}
