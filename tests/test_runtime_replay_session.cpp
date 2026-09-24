#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>

#include "delta_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace client = hlclient::client;
namespace fixture = hlclient::test::delta_fixture;

[[nodiscard]] constexpr std::uint32_t goldsrc_signed(
    const std::uint32_t magnitude,
    const bool negative = false) noexcept
{
    return (magnitude << 1U) | (negative ? 1U : 0U);
}

constexpr fixture::Field kEntityFields[]{
    {"origin[0]", 0x8000'0004U, 0U, 16U, 32'000U, 4'000U},
    {"origin[1]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
    {"origin[2]", 0x8000'0004U, 8U, 16U, 32'000U, 4'000U},
    {"angles[0]", 0x0000'0010U, 12U, 16U},
    {"angles[1]", 0x0000'0010U, 16U, 16U},
    {"angles[2]", 0x0000'0010U, 20U, 16U},
};
constexpr fixture::Field kClientFields[]{
    {"health", 0x8000'0004U, 0U, 10U},
    {"velocity[0]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
    {"velocity[1]", 0x8000'0004U, 8U, 16U, 32'000U, 4'000U},
    {"velocity[2]", 0x8000'0004U, 12U, 16U, 32'000U, 4'000U},
    {"view_ofs[2]", 0x8000'0004U, 16U, 10U, 16'000U, 4'000U},
    {"origin[0]", 0x8000'0004U, 20U, 16U, 32'000U, 4'000U},
    {"origin[1]", 0x8000'0004U, 24U, 16U, 32'000U, 4'000U},
    {"origin[2]", 0x8000'0004U, 28U, 16U, 32'000U, 4'000U},
};
constexpr fixture::Field kWeaponFields[]{
    {"m_iClip", 0x8000'0008U, 0U, 10U},
    {"m_fInReload", 0x0000'0008U, 4U, 1U},
    {"m_flNextReload", 0x8000'0004U, 8U, 22U, 4'000'000U, 4'000U},
    {"m_flNextPrimaryAttack", 0x8000'0004U, 12U, 22U,
     4'000'000U, 4'000U},
};

[[nodiscard]] std::shared_ptr<const goldsrc::DeltaSchemaRegistryState>
make_schemas(const bool mismatched_health = false,
    const std::optional<std::uint32_t> model_field_type = std::nullopt,
    const bool prediction_client_fields = false)
{
    const fixture::Field mismatched_client[]{
        {"health", 0x8000'0008U, 0U, 10U},
    };
    goldsrc::DeltaSchemaRegistryBuilder builder;
    std::vector<fixture::Field> entity_fields{std::begin(kEntityFields),std::end(kEntityFields)};
    std::vector<fixture::Field> client_fields{std::begin(kClientFields),std::end(kClientFields)};
    std::vector<fixture::Field> player_fields{std::begin(kEntityFields),std::end(kEntityFields)};
    if (model_field_type) { entity_fields.push_back({"modelindex",*model_field_type,600U,16U}); }
    if (prediction_client_fields) {
        client_fields.push_back({"flags", 0x0000'0008U, 32U, 32U});
        client_fields.push_back({"maxspeed", 0x0000'0004U, 36U, 16U, 10U, 1U});
        client_fields.push_back({"flDuckTime", 0x0000'0008U, 40U, 10U});
        client_fields.push_back({"bInDuck", 0x0000'0008U, 44U, 1U});
        client_fields.push_back({"waterlevel", 0x0000'0008U, 48U, 2U});
        client_fields.push_back({"deadflag", 0x0000'0008U, 52U, 3U});
        player_fields.push_back({"movetype", 0x0000'0008U, 24U, 4U});
        player_fields.push_back({"friction", 0x8000'0004U, 28U, 16U, 8U, 1U});
        player_fields.push_back({"usehull", 0x0000'0008U, 32U, 1U});
        player_fields.push_back({"gravity", 0x8000'0004U, 36U, 16U, 32U, 1U});
        player_fields.push_back({"basevelocity[0]", 0x8000'0004U, 40U, 16U, 8U, 1U});
        player_fields.push_back({"basevelocity[1]", 0x8000'0004U, 44U, 16U, 8U, 1U});
        player_fields.push_back({"basevelocity[2]", 0x8000'0004U, 48U, 16U, 8U, 1U});
        player_fields.push_back({"spectator", 0x0000'0008U, 52U, 1U});
    }
    for (const auto& encoded : std::array{
             fixture::schema("entity_state_t", entity_fields),
             fixture::schema(
                 "clientdata_t",
                 mismatched_health
                     ? std::span<const fixture::Field>{mismatched_client}
                     : std::span<const fixture::Field>{client_fields}),
             fixture::schema("weapon_data_t", kWeaponFields)}) {
        const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(encoded, 0U);
        REQUIRE(parsed);
        REQUIRE(parsed.schema);
        REQUIRE(builder.insert(*parsed.schema));
    }
    if (prediction_client_fields) {
        const auto encoded = fixture::schema("entity_state_player_t", player_fields);
        const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(encoded, 0U);
        REQUIRE(parsed);
        REQUIRE(parsed.schema);
        REQUIRE(builder.insert(*parsed.schema));
    }
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

[[nodiscard]] goldsrc::OwnedServicePayload payload(
    std::vector<std::byte> bytes,
    const std::uint32_t sequence)
{
    auto result = fixture::owning_payload(std::move(bytes));
    result.source_sequence = sequence;
    result.source_acknowledgement = sequence == 0U ? 0U : sequence - 1U;
    return result;
}

void delta(
    fixture::BitWriter& writer,
    const std::uint8_t mask,
    const std::span<const std::pair<std::uint32_t, std::size_t>> values = {})
{
    writer.write(mask == 0U ? 0U : 1U, 3U);
    if (mask != 0U) {
        writer.write(mask, 8U);
        for (const auto [value, width] : values) {
            writer.write(value, width);
        }
    }
}

[[nodiscard]] std::vector<std::byte> baseline_bytes(
    const bool include_player = false)
{
    fixture::BitWriter writer;
    if (include_player) {
        writer.write(1U, 11U);
        writer.write(0U, 2U);
        delta(writer, 0U);
    }
    for (const std::uint32_t number : {2U, 10U, 20U, 30U}) {
        writer.write(number, 11U);
        writer.write(0U, 2U);
        delta(writer, 0U);
    }
    writer.write(0xffffU, 16U);
    writer.write(0U, 6U);
    writer.align_zero();
    return writer.bytes();
}

[[nodiscard]] std::shared_ptr<const goldsrc::EntityBaselineRegistryState>
make_baselines(
    const goldsrc::DeltaSchemaRegistryState& schemas,
    const std::uint64_t generation,
    const bool include_player = false)
{
    auto source = payload(baseline_bytes(include_player), 17U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    REQUIRE(cursor);
    const auto decoded = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        goldsrc::EntityBaselineDecodeInput{
            &source, *cursor, 1U, generation, 1U,
            "entity_state_t",
            include_player ? "entity_state_player_t" : "entity_state_t",
            "entity_state_t",
            source.bytes.size() * 8U},
        schemas);
    REQUIRE(decoded);
    REQUIRE(decoded.registry);
    CHECK(decoded.entity_count == (include_player ? 5U : 4U));
    return std::make_shared<const goldsrc::EntityBaselineRegistryState>(
        std::move(*decoded.registry));
}

void time_prefix(fixture::BitWriter& writer, const float time)
{
    writer.write(7U, 8U);
    writer.write(std::bit_cast<std::uint32_t>(time), 32U);
}

void client_no_base(
    fixture::BitWriter& writer,
    const std::uint32_t health,
    const std::uint32_t velocity_x,
    const std::uint32_t view_z,
    const std::uint8_t clip)
{
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U);
    const std::pair<std::uint32_t, std::size_t> client_values[]{
        {goldsrc_signed(health), 10U},
        {goldsrc_signed(velocity_x), 16U},
        {goldsrc_signed(view_z), 10U}};
    delta(writer, 0x13U, client_values);
    writer.write(1U, 1U);
    writer.write(2U, 6U);
    const std::pair<std::uint32_t, std::size_t> weapon_values[]{
        {goldsrc_signed(clip), 10U}, {0U, 1U},
        {goldsrc_signed(1'500U), 22U}, {goldsrc_signed(250U), 22U}};
    delta(writer, 0x0fU, weapon_values);
    writer.write(0U, 1U);
    writer.align_zero();
}

void client_delta(
    fixture::BitWriter& writer,
    const std::uint8_t base_tag,
    const std::uint32_t health,
    const std::optional<std::uint8_t> clip)
{
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(1U, 1U);
    writer.write(base_tag, 8U);
    const std::pair<std::uint32_t, std::size_t> health_value[]{
        {goldsrc_signed(health), 10U}};
    delta(writer, 0x01U, health_value);
    if (clip) {
        writer.write(1U, 1U);
        writer.write(2U, 6U);
        const std::pair<std::uint32_t, std::size_t> clip_value[]{
            {goldsrc_signed(*clip), 10U}};
        delta(writer, 0x01U, clip_value);
    }
    writer.write(0U, 1U);
    writer.align_zero();
}

void entity_header(
    fixture::BitWriter& writer,
    const std::uint8_t opcode,
    const std::uint16_t count,
    const std::optional<std::uint8_t> base_tag = std::nullopt)
{
    writer.write(opcode, 8U);
    writer.write(count, 16U);
    if (base_tag) writer.write(*base_tag, 8U);
}

void full_entity(
    fixture::BitWriter& writer,
    const std::uint32_t difference,
    const std::uint32_t origin_x)
{
    writer.write(0U, 1U);
    writer.write(0U, 1U);
    writer.write(difference, 6U);
    writer.write(0U, 1U);
    writer.write(0U, 1U);
    const std::pair<std::uint32_t, std::size_t> value[]{
        {goldsrc_signed(origin_x), 16U}};
    delta(writer, 0x01U, value);
}

[[nodiscard]] std::vector<std::byte> prediction_fields_with_player()
{
    fixture::BitWriter writer;
    time_prefix(writer, 111.0F);
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U);
    writer.write(2U, 3U);
    writer.write(0x3f00U, 16U);
    writer.write(1U << 9U, 32U);
    writer.write(2'700U, 16U);
    writer.write(0U, 10U);
    writer.write(0U, 1U);
    writer.write(0U, 2U);
    writer.write(0U, 3U);
    writer.write(0U, 1U);
    writer.align_zero();
    entity_header(writer, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 1U);
    writer.write(0U, 1U); // not removed
    writer.write(0U, 1U); // short entity difference
    writer.write(1U, 6U); // receiving player entity number 1
    writer.write(0U, 1U); // no custom delta
    writer.write(0U, 1U); // no instanced baseline
    writer.write(2U, 3U); // two delta mask bytes
    writer.write(0x3fc0U, 16U); // fields 6..13
    writer.write(3U, 4U); // MOVETYPE_WALK
    writer.write(goldsrc_signed(8U), 16U); // friction=1
    writer.write(0U, 1U); // standing usehull
    writer.write(goldsrc_signed(32U), 16U); // gravity=1
    writer.write(0U, 16U);
    writer.write(0U, 16U);
    writer.write(0U, 16U);
    writer.write(0U, 1U); // spectator false
    writer.write(0U, 16U);
    writer.align_zero();
    return writer.bytes();
}

void delta_entity(
    fixture::BitWriter& writer,
    const std::uint32_t difference,
    const std::uint32_t origin_x)
{
    writer.write(0U, 1U);
    writer.write(0U, 1U);
    writer.write(difference, 6U);
    writer.write(0U, 1U);
    const std::pair<std::uint32_t, std::size_t> value[]{
        {goldsrc_signed(origin_x), 16U}};
    delta(writer, 0x01U, value);
}

void remove_entity(fixture::BitWriter& writer, const std::uint32_t difference)
{
    writer.write(1U, 1U);
    writer.write(0U, 1U);
    writer.write(difference, 6U);
}

void finish_entities(fixture::BitWriter& writer)
{
    writer.write(0U, 16U);
    writer.align_zero();
}

[[nodiscard]] std::vector<std::byte> initial_mixed()
{
    fixture::BitWriter writer;
    time_prefix(writer, 100.0F);
    client_no_base(writer, 100U, 12U, 112U, 8U);
    entity_header(writer, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 3U);
    full_entity(writer, 2U, 80U);
    full_entity(writer, 8U, 160U);
    full_entity(writer, 10U, 240U);
    finish_entities(writer);
    writer.write(5U, 8U);
    writer.write(2U, 16U);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> mixed_delta()
{
    fixture::BitWriter writer;
    time_prefix(writer, 101.0F);
    client_delta(writer, 100U, 75U, 5U);
    entity_header(
        writer, goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U, 100U);
    delta_entity(writer, 2U, 96U);
    remove_entity(writer, 18U);
    delta_entity(writer, 10U, 320U);
    finish_entities(writer);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> client_only(
    const std::uint8_t tag,
    const std::uint32_t health)
{
    fixture::BitWriter writer;
    time_prefix(writer, 103.0F);
    client_delta(writer, tag, health, std::nullopt);
    writer.write(1U, 8U);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> receiving_client_origin_only()
{
    fixture::BitWriter writer;
    time_prefix(writer, 109.0F);
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U);
    const std::pair<std::uint32_t, std::size_t> origin_values[]{
        {goldsrc_signed(80U), 16U},
        {goldsrc_signed(160U), 16U},
        {goldsrc_signed(240U), 16U}};
    delta(writer, 0xe0U, origin_values);
    writer.write(0U, 1U);
    writer.align_zero();
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> receiving_client_prediction_fields()
{
    fixture::BitWriter writer;
    time_prefix(writer, 110.0F);
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U);
    writer.write(2U, 3U); // Two delta mask bytes for fields 8..13.
    writer.write(0x3f00U, 16U);
    writer.write(0x200U, 32U); // flags
    writer.write(2'700U, 16U); // unsigned maxspeed, scale 10
    writer.write(250U, 10U); // flDuckTime
    writer.write(1U, 1U); // bInDuck
    writer.write(0U, 2U); // dry waterlevel
    writer.write(0U, 3U); // alive deadflag
    writer.write(0U, 1U); // no weapon slots
    writer.align_zero();
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> entity_only(
    const std::uint8_t tag,
    const std::uint32_t origin_x)
{
    fixture::BitWriter writer;
    time_prefix(writer, 104.0F);
    entity_header(
        writer, goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U, tag);
    delta_entity(writer, 10U, origin_x);
    finish_entities(writer);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> entity_full_recovery()
{
    fixture::BitWriter writer;
    time_prefix(writer, 106.0F);
    entity_header(writer, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 1U);
    full_entity(writer, 2U, 400U);
    finish_entities(writer);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> client_no_base_recovery()
{
    fixture::BitWriter writer;
    time_prefix(writer, 108.0F);
    client_no_base(writer, 60U, 0U, 112U, 3U);
    return writer.bytes();
}

[[nodiscard]] goldsrc::RuntimeReplayRecord record(
    std::vector<std::byte> bytes,
    const std::uint32_t sequence,
    const std::uint64_t identity,
    const std::size_t ordinal,
    const std::uint64_t generation = 1U)
{
    auto owning = payload(std::move(bytes), sequence);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, owning.bytes.size());
    REQUIRE(cursor);
    return goldsrc::RuntimeReplayRecord{
        generation, identity, ordinal, std::move(owning), *cursor};
}

[[nodiscard]] goldsrc::RuntimeReplayInitialization initialization(
    const std::uint64_t generation = 1U,
    const std::size_t maximum_entities = 4'096U,
    const bool mismatched_health = false,
    const bool prediction_client_fields = false)
{
    auto schemas = make_schemas(mismatched_health, std::nullopt,
        prediction_client_fields);
    auto baselines = make_baselines(*schemas, generation,
        prediction_client_fields);
    goldsrc::RuntimeReplayInitialization result;
    result.generation = generation;
    result.max_clients = 1U;
    result.schemas = std::move(schemas);
    result.baselines = std::move(baselines);
    result.schema_bindings.player_entity = prediction_client_fields
        ? "entity_state_player_t" : "entity_state_t";
    result.schema_bindings.custom_entity = "entity_state_t";
    result.limits.maximum_projected_entities = maximum_entities;
    return result;
}

[[nodiscard]] const client::RuntimePacketEntityObservation& entity(
    const client::RuntimeClientObservationState& state,
    const std::uint32_t number)
{
    const auto found = std::find_if(
        state.packet_entities.begin(), state.packet_entities.end(),
        [number](const auto& item) { return item.entity_number == number; });
    REQUIRE(found != state.packet_entities.end());
    return *found;
}

TEST_CASE("Runtime replay reference profile and limits are explicit",
          "[goldsrc][runtime-replay][profile]")
{
    CHECK(goldsrc::valid_runtime_replay_limits({}));
    CHECK(goldsrc::to_string(
              goldsrc::RuntimeReplayCompatibilityProfile::
                  public_goldsrc48_runtime_replay_v1) ==
          "public_goldsrc48_runtime_replay_v1");
    CHECK(goldsrc::to_string(
              goldsrc::RuntimeReplaySpecificationSource::
                  public_protocol_reference) ==
          "public_protocol_reference");
    CHECK(goldsrc::to_string(
              goldsrc::RuntimeReplayStockVerification::
                  not_verified_against_stock_runtime_payload) ==
          "not_verified_against_stock_runtime_payload");
}

TEST_CASE("Runtime replay publishes mixed A/B/C/D bytes into ClientWorldState",
          "[goldsrc][runtime-replay][integration][literal-layout]")
{
    client::ClientWorldState world;
    const auto world_revision = world.world_revision();
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(), world);
    REQUIRE(initialized);
    REQUIRE(initialized.session);
    CHECK(world.runtime_publication_revision() == 1U);
    REQUIRE(world.runtime_observation());
    CHECK(world.runtime_observation()->packet_entities.empty());

    auto first_record = record(initial_mixed(), 100U, 1'001U, 1U);
    const auto first_bytes = first_record.payload.bytes;
    const auto first = initialized.session->apply_record(first_record);
    REQUIRE(first);
    REQUIRE(first.event);
    CHECK(first.event->decoded_batch.end_cursor.absolute_bit_offset() ==
          first_bytes.size() * 8U);
    CHECK(first.event->server_time_observed);
    CHECK(first.event->clientdata_observed);
    CHECK(first.event->entities_observed);
    REQUIRE(world.runtime_observation());
    const auto& state = *world.runtime_observation();
    CHECK(state.publication_revision == 2U);
    CHECK(state.server_time_seconds == Catch::Approx(100.0));
    CHECK(state.packet_entities.size() == 3U);
    REQUIRE(entity(state, 2U).origin.x);
    CHECK(*entity(state, 2U).origin.x == Catch::Approx(10.0));
    REQUIRE(state.receiving_client);
    REQUIRE(state.receiving_client->health);
    CHECK(*state.receiving_client->health == Catch::Approx(100.0));
    REQUIRE(state.receiving_client->velocity.x);
    CHECK(*state.receiving_client->velocity.x == Catch::Approx(1.5));
    CHECK_FALSE(state.receiving_client->view_offset.x);
    REQUIRE(state.receiving_client->view_offset.z);
    CHECK(*state.receiving_client->view_offset.z == Catch::Approx(28.0));
    REQUIRE(state.weapon_slots[2U].clip);
    CHECK(*state.weapon_slots[2U].clip == 8);
    CHECK(state.entity_metadata.freshness ==
          client::RuntimeObservationFreshness::observed_in_record);
    CHECK(state.client_metadata.freshness ==
          client::RuntimeObservationFreshness::observed_in_record);
    CHECK(world.world_revision() == world_revision);

    first_record.payload.bytes.clear();
    CHECK(*world.runtime_observation()->receiving_client->health ==
          Catch::Approx(100.0));
    CHECK(*entity(*world.runtime_observation(), 2U).origin.x ==
          Catch::Approx(10.0));

    const auto second = initialized.session->apply_record(
        record(mixed_delta(), 101U, 1'002U, 2U));
    REQUIRE(second);
    REQUIRE(world.runtime_observation());
    const auto& changed = *world.runtime_observation();
    CHECK(changed.packet_entities.size() == 3U);
    REQUIRE(entity(changed, 2U).origin.x);
    REQUIRE(entity(changed, 10U).origin.x);
    REQUIRE(entity(changed, 30U).origin.x);
    REQUIRE(changed.receiving_client->health);
    CHECK(*entity(changed, 2U).origin.x == Catch::Approx(12.0));
    CHECK(*entity(changed, 10U).origin.x == Catch::Approx(20.0));
    CHECK(*entity(changed, 30U).origin.x == Catch::Approx(40.0));
    CHECK(*changed.receiving_client->health == Catch::Approx(75.0));
    CHECK(changed.weapon_slots[2U].clip == 5);
    CHECK(world.world_revision() == world_revision);
}

TEST_CASE("Runtime replay preserves partial-substate identity and older bases",
          "[goldsrc][runtime-replay][freshness][base]")
{
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(), world);
    REQUIRE(initialized);
    REQUIRE(initialized.session->apply_record(
        record(initial_mixed(), 100U, 2'001U, 1U)));
    REQUIRE(initialized.session->apply_record(
        record(mixed_delta(), 101U, 2'002U, 2U)));

    const auto client = initialized.session->apply_record(
        record(client_only(100U, 80U), 103U, 2'003U, 3U));
    REQUIRE(client);
    CHECK(client.event->clientdata_observed);
    CHECK_FALSE(client.event->entities_observed);
    REQUIRE(world.runtime_observation());
    CHECK(world.runtime_observation()->client_metadata.freshness ==
          client::RuntimeObservationFreshness::observed_in_record);
    CHECK(world.runtime_observation()->entity_metadata.freshness ==
          client::RuntimeObservationFreshness::retained);
    CHECK(world.runtime_observation()->client_metadata.source->
              source_transport_sequence == 103U);
    CHECK(world.runtime_observation()->entity_metadata.source->
              source_transport_sequence == 101U);
    REQUIRE(world.runtime_observation()->receiving_client->health);
    CHECK(*world.runtime_observation()->receiving_client->health ==
          Catch::Approx(80.0));
    CHECK(world.runtime_observation()->weapon_slots[2U].clip == 8);

    const auto entities = initialized.session->apply_record(
        record(entity_only(100U, 176U), 104U, 2'004U, 4U));
    REQUIRE(entities);
    CHECK_FALSE(entities.event->clientdata_observed);
    CHECK(entities.event->entities_observed);
    REQUIRE(world.runtime_observation());
    CHECK(world.runtime_observation()->client_metadata.freshness ==
          client::RuntimeObservationFreshness::retained);
    CHECK(world.runtime_observation()->entity_metadata.freshness ==
          client::RuntimeObservationFreshness::observed_in_record);
    REQUIRE(entity(*world.runtime_observation(), 10U).origin.x);
    REQUIRE(entity(*world.runtime_observation(), 20U).origin.x);
    CHECK(*entity(*world.runtime_observation(), 10U).origin.x ==
          Catch::Approx(22.0));
    CHECK(*entity(*world.runtime_observation(), 20U).origin.x ==
          Catch::Approx(30.0));
}

TEST_CASE("Runtime replay rejects missing bases and malformed records atomically",
          "[goldsrc][runtime-replay][transaction][recovery]")
{
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(), world);
    REQUIRE(initialized);
    REQUIRE(initialized.session->apply_record(
        record(initial_mixed(), 100U, 3'001U, 1U)));
    const auto committed = world.runtime_observation();
    const auto entity_history =
        initialized.session->decoder_state().history().snapshot_count();
    const auto client_history = initialized.session->decoder_state()
                                    .client_data_state().history().frame_count();

    const auto missing_entity = initialized.session->apply_record(
        record(entity_only(99U, 200U), 105U, 3'002U, 2U));
    REQUIRE_FALSE(missing_entity);
    REQUIRE(missing_entity.error);
    CHECK(missing_entity.error->code ==
          goldsrc::RuntimeReplayErrorCode::decoder_failed);
    CHECK(missing_entity.error->recovery ==
          goldsrc::RuntimeReplayRecoveryStatus::
              entity_full_snapshot_required);
    CHECK(world.runtime_observation() == committed);
    CHECK(initialized.session->decoder_state().history().snapshot_count() ==
          entity_history);

    REQUIRE(initialized.session->apply_record(
        record(entity_full_recovery(), 106U, 3'003U, 3U)));
    const auto after_entity_recovery = world.runtime_observation();
    const auto missing_client = initialized.session->apply_record(
        record(client_only(99U, 50U), 107U, 3'004U, 4U));
    REQUIRE_FALSE(missing_client);
    REQUIRE(missing_client.error);
    CHECK(missing_client.error->recovery ==
          goldsrc::RuntimeReplayRecoveryStatus::
              clientdata_no_base_required);
    CHECK(world.runtime_observation() == after_entity_recovery);
    CHECK(initialized.session->decoder_state()
              .client_data_state().history().frame_count() == client_history);

    REQUIRE(initialized.session->apply_record(
        record(client_no_base_recovery(), 108U, 3'005U, 5U)));
    const auto before_malformed = world.runtime_observation();
    auto malformed = client_no_base_recovery();
    malformed.push_back(std::byte{0xff});
    const auto malformed_opcode_offset = malformed.size() - 1U;
    const auto bad = initialized.session->apply_record(
        record(std::move(malformed), 109U, 3'006U, 6U));
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error);
    CHECK(bad.error->decoder_error ==
          goldsrc::PacketEntityDecodeErrorCode::unsupported_opcode);
    REQUIRE(bad.error->decoder_cursor);
    CHECK(bad.error->decoder_cursor->byte_offset() ==
          malformed_opcode_offset);
    CHECK(bad.error->decoder_cursor->bit_offset() == 0U);
    CHECK(bad.error->decoder_wire_opcode == 0xffU);
    CHECK(world.runtime_observation() == before_malformed);
}

TEST_CASE("Runtime replay bridge failures duplicates and generation reset are typed",
          "[goldsrc][runtime-replay][identity][generation][bridge]")
{
    SECTION("bridge projection limit rolls back decoder histories") {
        client::ClientWorldState world;
        auto selected = initialization(1U, 1U);
        selected.limits.decoder.snapshots.maximum_snapshot_history = 1U;
        auto initialized = goldsrc::RuntimeReplaySession::initialize(
            std::move(selected), world);
        REQUIRE(initialized);
        REQUIRE(initialized.session->apply_record(
            record(entity_full_recovery(), 90U, 4'000U, 1U)));
        REQUIRE(initialized.session->decoder_state().current_snapshot());
        const auto retained =
            initialized.session->decoder_state().current_snapshot();
        CHECK(initialized.session->decoder_state().history().snapshot_count() ==
              1U);
        const auto rejected = initialized.session->apply_record(
            record(initial_mixed(), 100U, 4'001U, 2U));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::RuntimeReplayErrorCode::projection_limit_exceeded);
        CHECK(world.runtime_publication_revision() == 2U);
        CHECK(initialized.session->decoder_state().history().snapshot_count() ==
              1U);
        CHECK(initialized.session->decoder_state().current_snapshot() ==
              retained);
        CHECK(initialized.session->decoder_state()
                  .client_data_state().history().frame_count() == 0U);
    }

    SECTION("record identity and generation domains remain distinct") {
        client::ClientWorldState world;
        auto initialized = goldsrc::RuntimeReplaySession::initialize(
            initialization(), world);
        REQUIRE(initialized);
        const auto first = record(initial_mixed(), 100U, 4'101U, 1U);
        REQUIRE(initialized.session->apply_record(first));
        const auto committed = world.runtime_observation();
        const auto duplicate = initialized.session->apply_record(first);
        REQUIRE_FALSE(duplicate);
        CHECK(duplicate.error->code ==
              goldsrc::RuntimeReplayErrorCode::duplicate_record);
        auto conflict = first;
        conflict.payload.bytes.back() ^= std::byte{1U};
        const auto conflicting = initialized.session->apply_record(conflict);
        REQUIRE_FALSE(conflicting);
        CHECK(conflicting.error->code ==
              goldsrc::RuntimeReplayErrorCode::conflicting_record);
        CHECK(world.runtime_observation() == committed);

        REQUIRE_FALSE(initialized.session->reset_generation(
            initialization(2U)));
        CHECK(world.runtime_observation()->generation == 2U);
        CHECK(world.runtime_observation()->packet_entities.empty());
        CHECK_FALSE(world.runtime_observation()->receiving_client);
        CHECK(initialized.session->decoder_state().history().snapshot_count() ==
              0U);
        const auto stale = initialized.session->apply_record(
            record(client_only(100U, 40U), 101U, 4'102U, 1U, 1U));
        REQUIRE_FALSE(stale);
        CHECK(stale.error->code ==
              goldsrc::RuntimeReplayErrorCode::generation_mismatch);
    }

    SECTION("wrong semantic descriptor is not treated as a missing field") {
        client::ClientWorldState world;
        auto initialized = goldsrc::RuntimeReplaySession::initialize(
            initialization(1U, 4'096U, true), world);
        REQUIRE(initialized);
        fixture::BitWriter writer;
        time_prefix(writer, 1.0F);
        writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
        writer.write(0U, 1U);
        const std::pair<std::uint32_t, std::size_t> value[]{{10U, 10U}};
        delta(writer, 0x01U, value);
        writer.write(0U, 1U);
        writer.align_zero();
        const auto rejected = initialized.session->apply_record(
            record(writer.bytes(), 20U, 4'201U, 1U));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::RuntimeReplayErrorCode::semantic_schema_mismatch);
        CHECK(world.runtime_publication_revision() == 1U);
    }

    SECTION("record identity retention stops at its configured bound") {
        client::ClientWorldState world;
        auto selected = initialization();
        selected.limits.maximum_record_fingerprints = 1U;
        auto initialized = goldsrc::RuntimeReplaySession::initialize(
            std::move(selected), world);
        REQUIRE(initialized);
        constexpr std::array nop{std::byte{1U}};
        REQUIRE(initialized.session->apply_record(record(
            std::vector<std::byte>{nop.begin(), nop.end()},
            50U, 4'301U, 1U)));
        const auto rejected = initialized.session->apply_record(record(
            std::vector<std::byte>{nop.begin(), nop.end()},
            51U, 4'302U, 2U));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::RuntimeReplayErrorCode::
                  record_identity_limit_exceeded);
        CHECK(world.runtime_publication_revision() == 2U);
    }
}

TEST_CASE("Replay visual model semantics require exact descriptor types and roll back mismatches",
          "[local-capture-assets][goldsrc][runtime-replay]")
{
    for (const auto model_type : {0x0000'0008U,0x0000'0002U}) {
        auto init=initialization();
        init.schemas=make_schemas(false,model_type);
        init.baselines=make_baselines(*init.schemas,1U);
        client::ClientWorldState world;
        auto session=goldsrc::RuntimeReplaySession::initialize(init,world);
        REQUIRE(session);
        const auto before=world.runtime_observation();
        fixture::BitWriter writer;
        time_prefix(writer,100.0F);
        entity_header(writer,goldsrc::kGoldSrcSvcPacketEntitiesOpcode,1U);
        writer.write(0U,1U); writer.write(0U,1U); writer.write(2U,6U);
        writer.write(0U,1U); writer.write(0U,1U);
        const std::pair<std::uint32_t,std::size_t> values[]{{7U,16U}};
        delta(writer,0x40U,values); finish_entities(writer);
        const auto applied=session.session->apply_record(record(writer.bytes(),100U,987U,1U));
        INFO((applied.error?applied.error->context:std::string{}));
        if (model_type==0x0000'0008U) {
            REQUIRE(applied); REQUIRE(world.runtime_observation()->packet_entities.size()==1U);
            const auto& entity=world.runtime_observation()->packet_entities.front();
            CHECK(entity.model_index==7U); CHECK(entity.ordinary_visual_schema);
            CHECK_FALSE(entity.sequence); CHECK_FALSE(entity.frame); CHECK_FALSE(entity.skin);
        } else {
            REQUIRE_FALSE(applied); REQUIRE(applied.error);
            CHECK(applied.error->code==goldsrc::RuntimeReplayErrorCode::semantic_schema_mismatch);
            CHECK(world.runtime_observation()==before);
        }
    }
}

TEST_CASE("Runtime replay canonical state is deterministic across presentation timing",
          "[goldsrc][runtime-replay][determinism]")
{
    auto run = [](const bool advance_between) {
        client::ClientWorldState world;
        auto initialized = goldsrc::RuntimeReplaySession::initialize(
            initialization(), world);
        REQUIRE(initialized);
        REQUIRE(initialized.session->apply_record(
            record(initial_mixed(), 100U, 5'001U, 1U)));
        if (advance_between) {
            world.advance(std::chrono::duration<double>{9.75});
        }
        REQUIRE(initialized.session->apply_record(
            record(mixed_delta(), 101U, 5'002U, 2U)));
        REQUIRE(world.runtime_observation());
        return std::pair{
            world.runtime_observation()->canonical_state_hash,
            world.runtime_observation()->publication_revision};
    };
    const auto immediate = run(false);
    const auto delayed = run(true);
    CHECK(immediate == delayed);

    auto invalid = *std::make_shared<client::RuntimeClientObservationState>();
    invalid.generation = 1U;
    invalid.publication_revision = 1U;
    invalid.server_time_seconds =
        (std::numeric_limits<double>::quiet_NaN)();
    CHECK_FALSE(client::valid_runtime_observation(invalid));
}

TEST_CASE("Runtime replay publishes exact receiving-client origin without entity inference",
          "[goldsrc][runtime-replay][clientdata][origin][live-usercmd]")
{
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(), world);
    REQUIRE(initialized);
    auto incoming = record(receiving_client_origin_only(), 109U, 5'100U, 1U);
    incoming.payload.source_reliable = false;
    incoming.payload.reassembled = false;
    REQUIRE(initialized.session->apply_record(incoming));
    REQUIRE(world.runtime_observation());
    const auto& observation = *world.runtime_observation();
    REQUIRE(observation.receiving_client);
    REQUIRE(observation.receiving_client->origin.complete());
    CHECK(*observation.receiving_client->origin.x == Catch::Approx(10.0));
    CHECK(*observation.receiving_client->origin.y == Catch::Approx(20.0));
    CHECK(*observation.receiving_client->origin.z == Catch::Approx(30.0));
    CHECK(observation.client_metadata.freshness ==
          client::RuntimeObservationFreshness::observed_in_record);
    REQUIRE(observation.client_metadata.source);
    CHECK(observation.client_metadata.source->source_transport_sequence == 109U);
    REQUIRE(observation.client_metadata.source->carrier_acknowledgement);
    CHECK(*observation.client_metadata.source->carrier_acknowledgement == 108U);
    CHECK_FALSE(observation.client_metadata.source->source_reliable);
    CHECK_FALSE(observation.client_metadata.source->reassembled);
    CHECK_FALSE(observation.receiving_client->flags);
    CHECK_FALSE(observation.receiving_client->maximum_speed);
    CHECK_FALSE(observation.receiving_client->in_duck);
    CHECK(observation.packet_entities.empty());
}

TEST_CASE("Runtime replay retains typed public clientdata prediction fields",
          "[goldsrc][runtime-replay][prediction-seed]")
{
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(1U, 4'096U, false, true), world);
    REQUIRE(initialized);
    REQUIRE(initialized.session->apply_record(record(
        receiving_client_prediction_fields(), 110U, 5'200U, 1U)));
    REQUIRE(world.runtime_observation());
    const auto& observed = *world.runtime_observation();
    REQUIRE(observed.receiving_client);
    REQUIRE(observed.receiving_client->flags);
    CHECK(*observed.receiving_client->flags == 0x200U);
    REQUIRE(observed.receiving_client->maximum_speed);
    CHECK(*observed.receiving_client->maximum_speed == Catch::Approx(270.0));
    REQUIRE(observed.receiving_client->duck_time);
    CHECK(*observed.receiving_client->duck_time == 250U);
    REQUIRE(observed.receiving_client->in_duck);
    CHECK(*observed.receiving_client->in_duck);
    REQUIRE(observed.receiving_client->water_level);
    CHECK(*observed.receiving_client->water_level == 0U);
    REQUIRE(observed.receiving_client->dead_flag);
    CHECK(*observed.receiving_client->dead_flag == 0U);
    // The declared origin fields reconstruct their default zero values even
    // though this particular delta changed only prediction fields.
    REQUIRE(observed.receiving_client->origin.complete());
    CHECK(*observed.receiving_client->origin.x == 0.0);
}

TEST_CASE("Runtime replay projects player movement fields in one committed record",
          "[goldsrc][runtime-replay][prediction-seed]")
{
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(1U, 4'096U, false, true), world);
    REQUIRE(initialized);
    const auto applied = initialized.session->apply_record(record(
        prediction_fields_with_player(), 111U, 5'201U, 1U));
    INFO((applied.error ? applied.error->context : "no replay error"));
    REQUIRE(applied);
    REQUIRE(world.runtime_observation());
    const auto& observed = *world.runtime_observation();
    REQUIRE(observed.client_metadata.source);
    REQUIRE(observed.entity_metadata.source);
    CHECK(observed.client_metadata.source->record_identity ==
          observed.entity_metadata.source->record_identity);
    REQUIRE(observed.packet_entities.size() == 1U);
    const auto& player = observed.packet_entities.front();
    CHECK(player.entity_number == 1U);
    CHECK(player.player_movement_schema);
    REQUIRE(player.move_type);
    CHECK(*player.move_type == 3U);
    REQUIRE(player.use_hull);
    CHECK(*player.use_hull == 0U);
    REQUIRE(player.gravity_multiplier);
    CHECK(*player.gravity_multiplier == Catch::Approx(1.0));
    REQUIRE(player.friction_multiplier);
    CHECK(*player.friction_multiplier == Catch::Approx(1.0));
    REQUIRE(player.base_velocity.complete());
    CHECK(*player.base_velocity.x == 0.0);
    REQUIRE(player.spectator);
    CHECK_FALSE(*player.spectator);
}

TEST_CASE("Replay record order identity and source sequence stay separate",
          "[goldsrc][runtime-replay][record-order]")
{
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(), world);
    REQUIRE(initialized);
    constexpr std::array nop{std::byte{1U}};
    REQUIRE(initialized.session->apply_record(
        record(std::vector<std::byte>{nop.begin(), nop.end()},
               50U, 6'001U, 1U)));
    const auto same_sequence = initialized.session->apply_record(
        record(std::vector<std::byte>{nop.begin(), nop.end()},
               50U, 6'002U, 2U));
    REQUIRE(same_sequence);
    CHECK(same_sequence.event->source_transport_sequence == 50U);
    CHECK(world.runtime_publication_revision() == 3U);

    const auto old = initialized.session->apply_record(
        record(std::vector<std::byte>{nop.begin(), nop.end()},
               51U, 6'003U, 1U));
    REQUIRE_FALSE(old);
    REQUIRE(old.error);
    CHECK(old.error->code == goldsrc::RuntimeReplayErrorCode::old_record);
    CHECK(world.runtime_publication_revision() == 3U);

    initialized.session->finish();
    CHECK(initialized.session->status() ==
          goldsrc::RuntimeReplaySessionStatus::finished);
    const auto after_finish = initialized.session->apply_record(
        record(std::vector<std::byte>{nop.begin(), nop.end()},
               52U, 6'004U, 3U));
    REQUIRE_FALSE(after_finish);
    CHECK(after_finish.error->code ==
          goldsrc::RuntimeReplayErrorCode::session_finished);
}

TEST_CASE("Runtime replay publishes svc_setangle as a fresh one-shot correction",
          "[goldsrc][runtime-replay][view-angle][live-visual]") {
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(
        initialization(), world);
    REQUIRE(initialized);
    const std::vector<std::byte> correction{
        std::byte{10U}, std::byte{0x00U}, std::byte{0x20U},
        std::byte{0x00U}, std::byte{0x40U}, std::byte{0x00U},
        std::byte{0x00U}};
    REQUIRE(initialized.session->apply_record(
        record(correction, 60U, 7'001U, 1U)));
    REQUIRE(world.runtime_observation());
    REQUIRE(world.runtime_observation()->view_angle_correction);
    CHECK(world.runtime_observation()->view_angle_correction->pitch_degrees ==
          Catch::Approx(45.0));
    CHECK(world.runtime_observation()->view_angle_correction->yaw_degrees ==
          Catch::Approx(90.0));
    CHECK(world.runtime_observation()->view_angle_correction->source
              .source_transport_sequence == 60U);
    const auto legacy_hash = world.runtime_observation()->canonical_state_hash;

    constexpr std::array nop{std::byte{1U}};
    REQUIRE(initialized.session->apply_record(
        record(std::vector<std::byte>{nop.begin(), nop.end()},
               61U, 7'002U, 2U)));
    REQUIRE(world.runtime_observation());
    CHECK_FALSE(world.runtime_observation()->view_angle_correction);
    CHECK(world.runtime_observation()->canonical_state_hash == legacy_hash);
}


} // namespace
