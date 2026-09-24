#include <hlclient/goldsrc/client_data_decoder.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/packet_entity_decoder.hpp>

#include "delta_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace fixture = hlclient::test::delta_fixture;

[[nodiscard]] constexpr std::uint32_t goldsrc_signed(
    const std::uint32_t magnitude,
    const bool negative = false) noexcept
{
    return (magnitude << 1U) | (negative ? 1U : 0U);
}

[[nodiscard]] std::vector<std::byte> no_base_with_weapons()
{
    fixture::BitWriter writer;
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U); // no base
    writer.write(1U, 3U); writer.write(0x01U, 8U);
    writer.write(goldsrc_signed(100U), 10U);
    writer.write(1U, 1U); writer.write(0U, 6U);
    writer.write(1U, 3U); writer.write(0x03U, 8U);
    writer.write(goldsrc_signed(1U, true), 10U); writer.write(7U, 6U);
    writer.write(1U, 1U); writer.write(63U, 6U);
    writer.write(1U, 3U); writer.write(0x01U, 8U);
    writer.write(goldsrc_signed(30U), 10U);
    writer.write(0U, 1U); writer.align_zero();
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> delta_from_100()
{
    fixture::BitWriter writer;
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(1U, 1U); writer.write(100U, 8U);
    writer.write(1U, 3U); writer.write(0x01U, 8U);
    writer.write(goldsrc_signed(90U), 10U);
    writer.write(1U, 1U); writer.write(0U, 6U);
    writer.write(1U, 3U); writer.write(0x01U, 8U);
    writer.write(goldsrc_signed(5U), 10U);
    writer.write(0U, 1U); writer.align_zero();
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> older_base_from_100()
{
    fixture::BitWriter writer;
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(1U, 1U); writer.write(100U, 8U);
    writer.write(0U, 3U);
    writer.write(1U, 1U); writer.write(63U, 6U);
    writer.write(1U, 3U); writer.write(0x01U, 8U);
    writer.write(goldsrc_signed(40U), 10U);
    writer.write(0U, 1U); writer.align_zero();
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> reset_slot_from_100()
{
    fixture::BitWriter writer;
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(1U, 1U); writer.write(100U, 8U);
    writer.write(0U, 3U);
    writer.write(1U, 1U); writer.write(0U, 6U);
    writer.write(1U, 3U); writer.write(0x03U, 8U);
    writer.write(0U, 10U); writer.write(0U, 6U);
    writer.write(0U, 1U); writer.align_zero();
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> rich_values()
{
    fixture::BitWriter writer;
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U); // no base
    writer.write(1U, 3U); writer.write(0x0fU, 8U);
    writer.write(goldsrc_signed(5U, true), 10U);
    writer.write(goldsrc_signed(6U, true), 12U); // -6 * 0.5 = -3
    writer.string("p");
    writer.write(25U, 8U); // 100 - 25/100 = 99.75
    writer.write(1U, 1U); writer.write(63U, 6U);
    writer.write(1U, 3U); writer.write(0x03U, 8U);
    writer.write(goldsrc_signed(1U, true), 10U);
    writer.write(goldsrc_signed(125U, true), 12U); // -125 * .001
    writer.write(0U, 1U); writer.align_zero();
    return writer.bytes();
}

// Literal service-message fixtures use the public GoldSrc 48 layout: opcode
// 15, LSB-first base flag/tag, client delta, repeated 1+6-bit weapon records,
// a zero continuation terminator, then zero padding through the next byte.
constexpr std::array kNoBaseEmpty{
    std::byte{0x0f}, std::byte{0x00}};
const auto kNoBaseWithWeapons = no_base_with_weapons();
const auto kDeltaFrom100 = delta_from_100();
const auto kOlderBaseFrom100 = older_base_from_100();
const auto kResetSlotFrom100 = reset_slot_from_100();
constexpr std::array kMissingBase99{
    std::byte{0x0f}, std::byte{0xc7}, std::byte{0x00}};
constexpr std::array kBaseTag100ZeroChanges{
    std::byte{0x0f}, std::byte{0xc9}, std::byte{0x00}};
constexpr std::array kDuplicateIndex{
    std::byte{0x0f}, std::byte{0x50}, std::byte{0x48}, std::byte{0x40},
    std::byte{0x00}, std::byte{0x85}, std::byte{0x04}, std::byte{0x08},
    std::byte{0x00}};
constexpr std::array kOutOfOrderIndex{
    std::byte{0x0f}, std::byte{0x70}, std::byte{0x48}, std::byte{0x40},
    std::byte{0x00}, std::byte{0x85}, std::byte{0x04}, std::byte{0x08},
    std::byte{0x00}};
const auto kRichValues = rich_values();

[[nodiscard]] std::vector<std::byte> mixed_client_and_entities()
{
    std::vector<std::byte> bytes{
        std::byte{0x07}, std::byte{0x00}, std::byte{0x00},
        std::byte{0xc8}, std::byte{0x42}};
    bytes.insert(
        bytes.end(), kNoBaseWithWeapons.begin(), kNoBaseWithWeapons.end());
    constexpr std::array entity_suffix{
        std::byte{0x28}, std::byte{0x05}, std::byte{0x00},
        std::byte{0x91}, std::byte{0x80}, std::byte{0x05},
        std::byte{0x12}, std::byte{0x24}, std::byte{0xc0},
        std::byte{0x42}, std::byte{0x0a}, std::byte{0x30},
        std::byte{0x48}, std::byte{0x40}, std::byte{0x08},
        std::byte{0x8a}, std::byte{0x40}, std::byte{0x02},
        std::byte{0x96}, std::byte{0x50}, std::byte{0x92},
        std::byte{0x00}, std::byte{0x16}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x05}, std::byte{0x01},
        std::byte{0x00}};
    bytes.insert(bytes.end(), entity_suffix.begin(), entity_suffix.end());
    return bytes;
}

constexpr fixture::Field kClientFields[]{
    {"health", 0x8000'0008U, 0U, 10U},
};
constexpr fixture::Field kWeaponFields[]{
    {"m_iClip", 0x8000'0008U, 0U, 10U},
    {"m_iId", 0x0000'0008U, 4U, 6U},
};
constexpr fixture::Field kRichClientFields[]{
    {"health", 0x8000'0008U, 0U, 10U},
    {"velocity[0]", 0x8000'0004U, 4U, 12U, 8'000U, 4'000U},
    {"physinfo", 0x0000'0080U, 8U, 1U},
    {"observed_at", 0x0000'0020U, 12U, 8U},
};
constexpr fixture::Field kRichWeaponFields[]{
    {"m_iClip", 0x8000'0008U, 0U, 10U},
    {"m_flNextPrimaryAttack", 0x8000'0004U, 4U, 12U,
     4'000'000U, 4'000U},
};

constexpr std::array kEntitySchemaBytes{
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
const auto kMixedClientAndEntities = mixed_client_and_entities();
constexpr std::array kClientOnlyStream{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x0f}, std::byte{0x00}, std::byte{0x05},
    std::byte{0x01}, std::byte{0x00}};
constexpr std::array kMalformedMixedSuffix{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x0f}, std::byte{0x12}, std::byte{0x40},
    std::byte{0x46}, std::byte{0x20}, std::byte{0x03}, std::byte{0xff},
    std::byte{0x1f}, std::byte{0xff}, std::byte{0x04}, std::byte{0x78},
    std::byte{0x00}, std::byte{0xfe}};
constexpr std::array kEntityOnlyStream{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x28}, std::byte{0x05}, std::byte{0x00},
    std::byte{0x91}, std::byte{0x80}, std::byte{0x05}, std::byte{0x12},
    std::byte{0x24}, std::byte{0xc0}, std::byte{0x42}, std::byte{0x0a},
    std::byte{0x30}, std::byte{0x48}, std::byte{0x40}, std::byte{0x08},
    std::byte{0x8a}, std::byte{0x40}, std::byte{0x02}, std::byte{0x96},
    std::byte{0x50}, std::byte{0x92}, std::byte{0x00}, std::byte{0x16},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, std::byte{0x01},
    std::byte{0x00}};
constexpr std::array kRepeatedClientdataStream{
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc8},
    std::byte{0x42}, std::byte{0x0f}, std::byte{0x00}, std::byte{0x0f},
    std::byte{0x00}};

[[nodiscard]] goldsrc::DeltaSchema parse_schema(
    const std::string_view name,
    const std::span<const fixture::Field> fields)
{
    const auto bytes = fixture::schema(name, fields);
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    return std::move(*parsed.schema);
}

[[nodiscard]] std::shared_ptr<const goldsrc::DeltaSchemaRegistryState>
make_schemas(const bool rich = false, const bool entities = false)
{
    goldsrc::DeltaSchemaRegistryBuilder builder;
    if (rich) {
        const auto client = parse_schema("clientdata_t", kRichClientFields);
        const auto alternate =
            parse_schema("clientdata_alt_t", kRichClientFields);
        const auto weapon = parse_schema("weapon_data_t", kRichWeaponFields);
        REQUIRE(builder.insert(client));
        REQUIRE(builder.insert(alternate));
        REQUIRE(builder.insert(weapon));
    } else {
        const auto client = parse_schema("clientdata_t", kClientFields);
        const auto alternate =
            parse_schema("clientdata_alt_t", kClientFields);
        const auto weapon = parse_schema("weapon_data_t", kWeaponFields);
        REQUIRE(builder.insert(client));
        REQUIRE(builder.insert(alternate));
        REQUIRE(builder.insert(weapon));
    }
    if (entities) {
        const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(
            kEntitySchemaBytes, 0U);
        REQUIRE(parsed);
        REQUIRE(parsed.schema);
        REQUIRE(builder.insert(*parsed.schema));
    }
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

[[nodiscard]] goldsrc::OwnedServicePayload owning_payload(
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence)
{
    auto payload = fixture::owning_payload(
        std::vector<std::byte>{bytes.begin(), bytes.end()});
    payload.source_sequence = sequence;
    return payload;
}

struct ClientFixture final {
    explicit ClientFixture(
        const bool rich = false,
        const goldsrc::ClientDataDecodeLimits selected = {})
        : schemas{make_schemas(rich)},
          state{1U, schemas, selected},
          decoder{selected}
    {
        REQUIRE(state.valid());
        REQUIRE(decoder.valid_configuration());
    }

    std::shared_ptr<const goldsrc::DeltaSchemaRegistryState> schemas;
    goldsrc::ClientDataSnapshotState state;
    goldsrc::GoldSrcClientDataDecoder decoder;
};

[[nodiscard]] goldsrc::ClientDataDecodeResult decode(
    ClientFixture& target,
    const std::span<const std::byte> bytes,
    const std::uint32_t sequence,
    const goldsrc::ClientDataReceiverMode mode =
        goldsrc::ClientDataReceiverMode::ordinary_game_client,
    const std::string_view client_schema = "clientdata_t")
{
    auto payload = owning_payload(bytes, sequence);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    return target.decoder.decode_one_and_apply(
        goldsrc::ClientDataDecodeInput{
            payload, *cursor, target.state.source_generation(), 4U, 0U,
            100.0, mode, client_schema, "weapon_data_t"},
        target.state);
}

[[nodiscard]] std::int32_t signed_value(
    const goldsrc::DeltaObjectState& object,
    const std::string_view field_name)
{
    const auto* field = object.find_exact(field_name);
    REQUIRE(field != nullptr);
    REQUIRE(std::holds_alternative<std::int32_t>(field->value()));
    return std::get<std::int32_t>(field->value());
}

[[nodiscard]] std::uint32_t unsigned_value(
    const goldsrc::DeltaObjectState& object,
    const std::string_view field_name)
{
    const auto* field = object.find_exact(field_name);
    REQUIRE(field != nullptr);
    REQUIRE(std::holds_alternative<std::uint32_t>(field->value()));
    return std::get<std::uint32_t>(field->value());
}

[[nodiscard]] double floating_value(
    const goldsrc::DeltaObjectState& object,
    const std::string_view field_name)
{
    const auto* field = object.find_exact(field_name);
    REQUIRE(field != nullptr);
    REQUIRE(std::holds_alternative<double>(field->value()));
    return std::get<double>(field->value());
}

[[nodiscard]] std::shared_ptr<const goldsrc::EntityBaselineRegistryState>
make_empty_baselines(const goldsrc::DeltaSchemaRegistryState& schemas)
{
    auto published = goldsrc::EntityBaselineRegistryBuilder{
        schemas, {},
        goldsrc::EntitySnapshotCompatibilityProfile::
            public_goldsrc48_entity_delta_v1}.publish();
    REQUIRE(published);
    return std::make_shared<const goldsrc::EntityBaselineRegistryState>(
        std::move(*published.state));
}

[[nodiscard]] std::shared_ptr<const goldsrc::EntityBaselineRegistryState>
make_entity_baselines(const goldsrc::DeltaSchemaRegistryState& schemas)
{
    auto payload = owning_payload(kBaselineBytes, 17U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    const auto decoded = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        goldsrc::EntityBaselineDecodeInput{
            &payload, *cursor, 0U, 1U, 1U, "x", "x", "x",
            kBaselineBytes.size() * 8U},
        schemas);
    REQUIRE(decoded);
    REQUIRE(decoded.registry);
    return std::make_shared<const goldsrc::EntityBaselineRegistryState>(
        std::move(*decoded.registry));
}

} // namespace

TEST_CASE("GoldSrc clientdata literal no-base and weapon records reconstruct owning state",
          "[goldsrc][clientdata][literal]")
{
    ClientFixture target;
    const auto empty = decode(target, kNoBaseEmpty, 90U);
    REQUIRE(empty);
    REQUIRE(empty.event);
    CHECK(empty.event->specification_source ==
          goldsrc::ClientDataSpecificationSource::public_protocol_reference);
    CHECK(empty.event->stock_verification ==
          goldsrc::ClientDataStockVerification::
              not_verified_against_stock_runtime_payload);
    CHECK_FALSE(empty.event->wire_delta_base_tag);
    CHECK(empty.event->end_cursor.byte_offset() == kNoBaseEmpty.size());
    CHECK(empty.event->end_cursor.bit_offset() == 0U);
    CHECK(signed_value(empty.event->frame->client_data(), "health") == 0);
    CHECK(empty.event->frame->weapon_slots().size() == 64U);
    CHECK(empty.event->frame->statistics().wire_weapon_record_count == 0U);

    const auto with_weapons = decode(target, kNoBaseWithWeapons, 100U);
    REQUIRE(with_weapons);
    const auto& frame = *with_weapons.event->frame;
    CHECK(signed_value(frame.client_data(), "health") == 100);
    REQUIRE(frame.wire_updated_weapon_indices().size() == 2U);
    CHECK(frame.wire_updated_weapon_indices()[0] == 0U);
    CHECK(frame.wire_updated_weapon_indices()[1] == 63U);
    REQUIRE(frame.find_weapon_slot(0U));
    REQUIRE(frame.find_weapon_slot(63U));
    CHECK(signed_value(frame.find_weapon_slot(0U)->object(), "m_iClip") == -1);
    CHECK(unsigned_value(frame.find_weapon_slot(0U)->object(), "m_iId") == 7U);
    CHECK(signed_value(frame.find_weapon_slot(63U)->object(), "m_iClip") == 30);
    CHECK(unsigned_value(frame.find_weapon_slot(63U)->object(), "m_iId") == 0U);
    CHECK(target.state.history().frame_count() == 2U);
}

TEST_CASE("Clientdata explicit base uses exact older frame and preserves omitted slots",
          "[goldsrc][clientdata][base][history]")
{
    ClientFixture target;
    REQUIRE(decode(target, kNoBaseWithWeapons, 100U));
    REQUIRE(decode(target, kNoBaseEmpty, 102U));

    const auto delta = decode(target, kDeltaFrom100, 103U);
    REQUIRE(delta);
    REQUIRE(delta.event->resolved_base);
    CHECK(delta.event->resolved_base->source_transport_sequence == 100U);
    const auto& frame = *delta.event->frame;
    REQUIRE(frame.base_reference());
    CHECK(frame.base_reference()->source_transport_sequence() == 100U);
    CHECK(signed_value(frame.client_data(), "health") == 90);
    CHECK(signed_value(frame.find_weapon_slot(0U)->object(), "m_iClip") == 5);
    CHECK(unsigned_value(frame.find_weapon_slot(0U)->object(), "m_iId") == 7U);
    CHECK(signed_value(frame.find_weapon_slot(63U)->object(), "m_iClip") == 30);
    CHECK(frame.statistics().omitted_weapon_slot_count == 63U);

    const auto older = decode(target, kOlderBaseFrom100, 105U);
    REQUIRE(older);
    CHECK(signed_value(older.event->frame->client_data(), "health") == 100);
    CHECK(signed_value(
              older.event->frame->find_weapon_slot(0U)->object(), "m_iClip") ==
          -1);
    CHECK(signed_value(
              older.event->frame->find_weapon_slot(63U)->object(), "m_iClip") ==
          40);

    const auto reset = decode(target, kResetSlotFrom100, 106U);
    REQUIRE(reset);
    CHECK(signed_value(
              reset.event->frame->find_weapon_slot(0U)->object(), "m_iClip") ==
          0);
    CHECK(unsigned_value(
              reset.event->frame->find_weapon_slot(0U)->object(), "m_iId") ==
          0U);
    CHECK(signed_value(
              reset.event->frame->find_weapon_slot(63U)->object(), "m_iClip") ==
          30);

    const auto zero_change = decode(target, kBaseTag100ZeroChanges, 108U);
    REQUIRE(zero_change);
    CHECK_FALSE(zero_change.event->frame->statistics().client_fields_changed);
    CHECK(zero_change.event->frame->statistics().wire_weapon_record_count == 0U);
    CHECK(zero_change.event->frame->statistics().changed_weapon_slot_count == 0U);
    CHECK(signed_value(
              zero_change.event->frame->find_weapon_slot(0U)->object(),
              "m_iClip") == -1);
}

TEST_CASE("Clientdata missing incompatible and generation-old bases fail atomically",
          "[goldsrc][clientdata][base][transaction]")
{
    ClientFixture target;
    REQUIRE(decode(target, kNoBaseWithWeapons, 100U));
    const auto committed = target.state.current_frame();
    const auto count = target.state.history().frame_count();

    const auto missing = decode(target, kMissingBase99, 103U);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code == goldsrc::ClientDataDecodeErrorCode::missing_delta_base);
    CHECK(missing.error->recovery ==
          goldsrc::ClientDataRecoveryStatus::no_base_message_required);
    CHECK(target.state.current_frame() == committed);
    CHECK(target.state.history().frame_count() == count);

    const auto incompatible = decode(
        target, kBaseTag100ZeroChanges, 104U,
        goldsrc::ClientDataReceiverMode::ordinary_game_client,
        "clientdata_alt_t");
    REQUIRE_FALSE(incompatible);
    REQUIRE(incompatible.error);
    CHECK(incompatible.error->code ==
          goldsrc::ClientDataDecodeErrorCode::incompatible_delta_base);
    CHECK(target.state.current_frame() == committed);

    REQUIRE(decode(target, kNoBaseEmpty, 105U));
    REQUIRE(target.state.reset_source_generation(2U, target.schemas));
    CHECK_FALSE(target.state.current_frame());
    CHECK(target.state.history().frame_count() == 0U);
    const auto old_generation = decode(target, kBaseTag100ZeroChanges, 106U);
    REQUIRE_FALSE(old_generation);
    REQUIRE(old_generation.error);
    CHECK(old_generation.error->code ==
          goldsrc::ClientDataDecodeErrorCode::missing_delta_base);
}

TEST_CASE("Clientdata signed quantized string and time-window values are literal",
          "[goldsrc][clientdata][numeric][literal]")
{
    ClientFixture target{true};
    const auto decoded = decode(target, kRichValues, 120U);
    const auto error_context = decoded.error
        ? decoded.error->context : std::string{"no error"};
    CAPTURE(error_context);
    REQUIRE(decoded);
    const auto& client = decoded.event->frame->client_data();
    CHECK(signed_value(client, "health") == -5);
    CHECK(floating_value(client, "velocity[0]") == Catch::Approx(-3.0));
    const auto* physinfo = client.find_exact("physinfo");
    REQUIRE(physinfo);
    REQUIRE(std::holds_alternative<std::string>(physinfo->value()));
    CHECK(std::get<std::string>(physinfo->value()) == "p");
    CHECK(floating_value(client, "observed_at") == Catch::Approx(99.75));
    const auto* weapon = decoded.event->frame->find_weapon_slot(63U);
    REQUIRE(weapon);
    CHECK(signed_value(weapon->object(), "m_iClip") == -1);
    CHECK(floating_value(weapon->object(), "m_flNextPrimaryAttack") ==
          Catch::Approx(-0.125));
}

TEST_CASE("Weapon list order terminator and safety boundaries are enforced",
          "[goldsrc][clientdata][weapon][negative]")
{
    SECTION("duplicate and descending indices are typed") {
        ClientFixture duplicate_target;
        const auto duplicate = decode(duplicate_target, kDuplicateIndex, 130U);
        REQUIRE_FALSE(duplicate);
        REQUIRE(duplicate.error);
        CHECK(duplicate.error->code ==
              goldsrc::ClientDataDecodeErrorCode::duplicate_weapon_index);
        CHECK_FALSE(duplicate_target.state.current_frame());

        ClientFixture order_target;
        const auto order = decode(order_target, kOutOfOrderIndex, 130U);
        REQUIRE_FALSE(order);
        REQUIRE(order.error);
        CHECK(order.error->code ==
              goldsrc::ClientDataDecodeErrorCode::out_of_order_weapon_index);
    }

    SECTION("all 64 indices still consume the explicit terminating flag") {
        fixture::BitWriter writer;
        writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
        writer.write(0U, 1U);
        writer.write(0U, 3U);
        for (std::uint32_t index = 0U; index < 64U; ++index) {
            writer.write(1U, 1U);
            writer.write(index, 6U);
            writer.write(0U, 3U);
        }
        writer.write(0U, 1U);
        writer.align_zero();
        ClientFixture target;
        const auto decoded = decode(target, writer.bytes(), 131U);
        REQUIRE(decoded);
        CHECK(decoded.event->frame->statistics().wire_weapon_record_count == 64U);
        CHECK(decoded.event->end_cursor.byte_offset() == writer.bytes().size());

        auto truncated_tail = writer.bytes();
        truncated_tail.pop_back();
        ClientFixture truncated_target;
        const auto truncated = decode(truncated_target, truncated_tail, 131U);
        REQUIRE_FALSE(truncated);
        REQUIRE(truncated.error);
        CHECK(truncated.error->code ==
              goldsrc::ClientDataDecodeErrorCode::truncated_weapon_index);
    }

    SECTION("nonzero padding and every rich fixture prefix are rejected") {
        constexpr std::array bad_padding{
            std::byte{0x0f}, std::byte{0x80}};
        ClientFixture padding_target;
        const auto padding = decode(padding_target, bad_padding, 132U);
        REQUIRE_FALSE(padding);
        REQUIRE(padding.error);
        CHECK(padding.error->code ==
              goldsrc::ClientDataDecodeErrorCode::malformed_padding);

        for (std::size_t size = 1U; size < kRichValues.size(); ++size) {
            ClientFixture prefix_target{true};
            const auto prefix = decode(
                prefix_target,
                std::span<const std::byte>{kRichValues.data(), size}, 133U);
            CHECK_FALSE(prefix);
            CHECK_FALSE(prefix_target.state.current_frame());
        }
    }
}

TEST_CASE("Clientdata history handles wrap eviction duplicates and recovery",
          "[goldsrc][clientdata][history][wrap]")
{
    auto limits = goldsrc::ClientDataDecodeLimits{};
    limits.maximum_history_frames = 2U;
    ClientFixture target{false, limits};
    REQUIRE(decode(target, kNoBaseWithWeapons, 255U));
    const auto invalid = decode(target, kBaseTag100ZeroChanges, 256U);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code ==
          goldsrc::ClientDataDecodeErrorCode::invalid_delta_base_lookback);

    constexpr std::array tag255{
        std::byte{0x0f}, std::byte{0xff}, std::byte{0x01}};
    REQUIRE(decode(target, tag255, 256U));
    REQUIRE(decode(target, kNoBaseEmpty, 257U));
    CHECK(target.state.history().frame_count() == 2U);
    const auto evicted = decode(target, tag255, 258U);
    REQUIRE_FALSE(evicted);
    REQUIRE(evicted.error);
    CHECK(evicted.error->code == goldsrc::ClientDataDecodeErrorCode::evicted_delta_base);

    const auto committed = target.state.current_frame();
    const auto duplicate = decode(target, kNoBaseEmpty, 257U);
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code == goldsrc::ClientDataDecodeErrorCode::duplicate_source_frame);
    CHECK(target.state.current_frame() == committed);

    const auto conflict = decode(target, kNoBaseWithWeapons, 257U);
    REQUIRE_FALSE(conflict);
    REQUIRE(conflict.error);
    CHECK(conflict.error->code ==
          goldsrc::ClientDataDecodeErrorCode::conflicting_source_frame);
    const auto old = decode(target, kNoBaseEmpty, 254U);
    REQUIRE_FALSE(old);
    REQUIRE(old.error);
    CHECK(old.error->code == goldsrc::ClientDataDecodeErrorCode::old_source_frame);
}

TEST_CASE("Clientdata configured record payload frame and history limits are transactional",
          "[goldsrc][clientdata][limit]")
{
    SECTION("wire record and payload limits") {
        auto record_limits = goldsrc::ClientDataDecodeLimits{};
        record_limits.maximum_weapon_records = 1U;
        ClientFixture records{false, record_limits};
        const auto record_failure = decode(records, kNoBaseWithWeapons, 135U);
        REQUIRE_FALSE(record_failure);
        REQUIRE(record_failure.error);
        CHECK(record_failure.error->code ==
              goldsrc::ClientDataDecodeErrorCode::weapon_record_limit_exceeded);
        CHECK_FALSE(records.state.current_frame());

        auto payload_limits = goldsrc::ClientDataDecodeLimits{};
        payload_limits.maximum_payload_bytes = 1U;
        ClientFixture payload_target{false, payload_limits};
        const auto payload_failure = decode(payload_target, kNoBaseEmpty, 136U);
        REQUIRE_FALSE(payload_failure);
        REQUIRE(payload_failure.error);
        CHECK(payload_failure.error->code ==
              goldsrc::ClientDataDecodeErrorCode::payload_too_large);
    }

    SECTION("aggregate history bound fails before a second publication") {
        ClientFixture probe;
        REQUIRE(decode(probe, kNoBaseEmpty, 1U));
        const auto one_frame_bytes =
            probe.state.current_frame()->statistics().accounted_value_bytes;
        auto limits = goldsrc::ClientDataDecodeLimits{};
        limits.maximum_frame_value_bytes = one_frame_bytes;
        limits.maximum_history_value_bytes = one_frame_bytes;
        ClientFixture bounded{false, limits};
        REQUIRE(decode(bounded, kNoBaseEmpty, 1U));
        const auto first = bounded.state.current_frame();
        const auto second = decode(bounded, kNoBaseEmpty, 2U);
        REQUIRE_FALSE(second);
        REQUIRE(second.error);
        CHECK(second.error->code ==
              goldsrc::ClientDataDecodeErrorCode::history_value_limit_exceeded);
        CHECK(bounded.state.current_frame() == first);
        CHECK(bounded.state.history().frame_count() == 1U);
    }
}

TEST_CASE("Proxy mode and malformed low-level inputs never use a guessed fallback",
          "[goldsrc][clientdata][mode][negative]")
{
    ClientFixture target;
    const auto proxy = decode(
        target, kNoBaseEmpty, 140U,
        goldsrc::ClientDataReceiverMode::proxy_or_hltv);
    REQUIRE_FALSE(proxy);
    REQUIRE(proxy.error);
    CHECK(proxy.error->code ==
          goldsrc::ClientDataDecodeErrorCode::unsupported_receiver_mode);
    CHECK_FALSE(target.state.current_frame());

    for (const auto bytes : std::array{
             std::span<const std::byte>{},
             std::span<const std::byte>{kNoBaseEmpty.data(), 1U}}) {
        ClientFixture truncated_target;
        const auto truncated = decode(truncated_target, bytes, 141U);
        REQUIRE_FALSE(truncated);
        REQUIRE(truncated.error);
        CHECK_FALSE(truncated_target.state.current_frame());
    }
}

TEST_CASE("Clientdata profile schema and server-time validation remain typed",
          "[goldsrc][clientdata][configuration]")
{
    ClientFixture target;
    auto source = owning_payload(kNoBaseEmpty, 150U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    REQUIRE(cursor);

    const auto invalid_profile = static_cast<
        goldsrc::ClientDataCompatibilityProfile>(0xffU);
    goldsrc::GoldSrcClientDataDecoder profile_decoder{{}, invalid_profile};
    CHECK_FALSE(profile_decoder.valid_configuration());
    const auto profile_result = profile_decoder.decode_one_and_apply(
        goldsrc::ClientDataDecodeInput{
            source, *cursor, 1U, 1U, 0U, 100.0},
        target.state);
    REQUIRE_FALSE(profile_result);
    REQUIRE(profile_result.error);
    CHECK(profile_result.error->code ==
          goldsrc::ClientDataDecodeErrorCode::invalid_profile);

    const auto missing_time = target.decoder.decode_one_and_apply(
        goldsrc::ClientDataDecodeInput{
            source, *cursor, 1U, 1U, 0U, std::nullopt},
        target.state);
    REQUIRE_FALSE(missing_time);
    REQUIRE(missing_time.error);
    CHECK(missing_time.error->code ==
          goldsrc::ClientDataDecodeErrorCode::missing_server_time);

    for (const double non_finite_time : {
             (std::numeric_limits<double>::infinity)(),
             (std::numeric_limits<double>::quiet_NaN)()}) {
        const auto non_finite = target.decoder.decode_one_and_apply(
            goldsrc::ClientDataDecodeInput{
                source, *cursor, 1U, 1U, 0U, non_finite_time},
            target.state);
        REQUIRE_FALSE(non_finite);
        REQUIRE(non_finite.error);
        CHECK(non_finite.error->code ==
              goldsrc::ClientDataDecodeErrorCode::missing_server_time);
    }

    const auto missing_schema = target.decoder.decode_one_and_apply(
        goldsrc::ClientDataDecodeInput{
            source, *cursor, 1U, 1U, 0U, 100.0,
            goldsrc::ClientDataReceiverMode::ordinary_game_client,
            "absent_clientdata_t", "weapon_data_t"},
        target.state);
    REQUIRE_FALSE(missing_schema);
    REQUIRE(missing_schema.error);
    CHECK(missing_schema.error->code ==
          goldsrc::ClientDataDecodeErrorCode::unknown_schema);
    CHECK_FALSE(target.state.current_frame());
}

TEST_CASE("Shared dispatcher publishes client and entity substates once per source frame",
          "[goldsrc][clientdata][packet-entities][transaction]")
{
    const auto schemas = make_schemas(false, true);
    const auto baselines = make_entity_baselines(*schemas);
    goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
    goldsrc::GoldSrcPacketEntityDecoder decoder;
    auto payload = owning_payload(kMixedClientAndEntities, 250U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    const auto decoded = decoder.decode_and_apply(
        goldsrc::PacketEntityDecodeInput{
            payload, *cursor, 1U, 9U, "x", "x", "x"},
        state);
    REQUIRE(decoded);
    REQUIRE(decoded.batch);
    CHECK(decoded.batch->end_cursor.byte_offset() == kMixedClientAndEntities.size());
    CHECK(decoded.batch->events.size() == 4U);
    REQUIRE(state.client_data_state().current_frame());
    REQUIRE(state.current_snapshot());
    CHECK(state.client_data_state().current_frame()->reference().source_transport_sequence() ==
          250U);
    CHECK(state.current_snapshot()->reference().value() == 250U);
    CHECK(state.current_snapshot()->entity_count() == 5U);
    CHECK(signed_value(
              state.client_data_state().current_frame()->client_data(), "health") ==
          100);

    SECTION("malformed entity body rolls back the earlier client body") {
        goldsrc::PacketEntitySnapshotState rollback_state{
            1U, 1U, schemas, baselines};
        auto malformed = std::vector<std::byte>{
            kMixedClientAndEntities.begin(), kMixedClientAndEntities.end()};
        malformed[41] = std::byte{0x80};
        auto malformed_payload = owning_payload(malformed, 251U);
        const auto malformed_cursor =
            goldsrc::StockRuntimeSourceCursor::create(
                0U, 0U, malformed_payload.bytes.size());
        REQUIRE(malformed_cursor);
        const auto rejected = decoder.decode_and_apply(
            goldsrc::PacketEntityDecodeInput{
                malformed_payload, *malformed_cursor, 1U, 10U,
                "x", "x", "x"},
            rollback_state);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::PacketEntityDecodeErrorCode::malformed_padding);
        CHECK_FALSE(rollback_state.client_data_state().current_frame());
        CHECK_FALSE(rollback_state.current_snapshot());
        CHECK_FALSE(rollback_state.control_state().server_time());
    }
}

TEST_CASE("Shared dispatcher supports client-only frame and rolls back malformed suffix",
          "[goldsrc][clientdata][dispatcher][transaction]")
{
    const auto schemas = make_schemas();
    const auto baselines = make_empty_baselines(*schemas);

    SECTION("client-only payload does not fabricate an entity observation") {
        goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
        goldsrc::GoldSrcPacketEntityDecoder decoder;
        auto payload = owning_payload(kClientOnlyStream, 160U);
        const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
            0U, 0U, payload.bytes.size());
        REQUIRE(cursor);
        const auto decoded = decoder.decode_and_apply(
            goldsrc::PacketEntityDecodeInput{payload, *cursor, 1U, 3U}, state);
        REQUIRE(decoded);
        CHECK(decoded.batch->events.size() == 3U);
        REQUIRE(state.client_data_state().current_frame());
        CHECK_FALSE(state.current_snapshot());
        CHECK(state.history().snapshot_count() == 0U);
    }

    SECTION("valid clientdata plus malformed suffix changes no substate") {
        goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
        goldsrc::GoldSrcPacketEntityDecoder decoder;
        auto payload = owning_payload(kMalformedMixedSuffix, 161U);
        const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
            0U, 0U, payload.bytes.size());
        REQUIRE(cursor);
        const auto decoded = decoder.decode_and_apply(
            goldsrc::PacketEntityDecodeInput{payload, *cursor, 1U, 4U}, state);
        REQUIRE_FALSE(decoded);
        REQUIRE(decoded.error);
        CHECK(decoded.error->code ==
              goldsrc::PacketEntityDecodeErrorCode::unsupported_opcode);
        CHECK_FALSE(state.client_data_state().current_frame());
        CHECK_FALSE(state.current_snapshot());
        CHECK_FALSE(state.control_state().server_time());
        CHECK(state.client_data_state().history().frame_count() == 0U);
    }
}

TEST_CASE("Entity-only frames and repeated client messages do not fabricate client bases",
          "[goldsrc][clientdata][dispatcher][identity]")
{
    SECTION("entity-only source frame is not a clientdata base") {
        const auto schemas = make_schemas(false, true);
        const auto baselines = make_entity_baselines(*schemas);
        goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
        goldsrc::GoldSrcPacketEntityDecoder decoder;
        auto entity_payload = owning_payload(kEntityOnlyStream, 200U);
        const auto entity_cursor = goldsrc::StockRuntimeSourceCursor::create(
            0U, 0U, entity_payload.bytes.size());
        REQUIRE(entity_cursor);
        REQUIRE(decoder.decode_and_apply(
            goldsrc::PacketEntityDecodeInput{
                entity_payload, *entity_cursor, 1U, 10U, "x", "x", "x"},
            state));
        REQUIRE(state.current_snapshot());
        CHECK_FALSE(state.client_data_state().current_frame());

        constexpr std::array base200{
            std::byte{0x0f}, std::byte{0x91}, std::byte{0x01}};
        auto client_payload = owning_payload(base200, 201U);
        const auto client_cursor = goldsrc::StockRuntimeSourceCursor::create(
            0U, 0U, client_payload.bytes.size());
        REQUIRE(client_cursor);
        const auto missing = decoder.decode_and_apply(
            goldsrc::PacketEntityDecodeInput{
                client_payload, *client_cursor, 1U, 11U, "x", "x", "x"},
            state);
        REQUIRE_FALSE(missing);
        REQUIRE(missing.error);
        REQUIRE(missing.error->clientdata_error);
        CHECK(*missing.error->clientdata_error ==
              goldsrc::ClientDataDecodeErrorCode::missing_delta_base);
        CHECK_FALSE(state.client_data_state().current_frame());
    }

    SECTION("two clientdata bodies in one source frame roll back the first") {
        const auto schemas = make_schemas();
        const auto baselines = make_empty_baselines(*schemas);
        goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
        goldsrc::GoldSrcPacketEntityDecoder decoder;
        auto repeated_payload = owning_payload(kRepeatedClientdataStream, 210U);
        const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
            0U, 0U, repeated_payload.bytes.size());
        REQUIRE(cursor);
        const auto repeated = decoder.decode_and_apply(
            goldsrc::PacketEntityDecodeInput{
                repeated_payload, *cursor, 1U, 12U},
            state);
        REQUIRE_FALSE(repeated);
        REQUIRE(repeated.error);
        REQUIRE(repeated.error->clientdata_error);
        CHECK(*repeated.error->clientdata_error ==
              goldsrc::ClientDataDecodeErrorCode::duplicate_source_frame);
        CHECK_FALSE(state.client_data_state().current_frame());
        CHECK_FALSE(state.control_state().server_time());
    }
}
