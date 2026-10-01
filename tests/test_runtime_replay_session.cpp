#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <hlclient/games/halflife/inventory.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>

#include "delta_test_fixture.hpp"
#include "event_test_fixture.hpp"

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
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace client = hlclient::client;
namespace fixture = hlclient::test::delta_fixture;
namespace event_fixture = hlclient::test::event_fixture;

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
    const bool prediction_client_fields = false,
    const bool brush_fields = false,
    const bool scripted_event_fields = false)
{
    const fixture::Field mismatched_client[]{
        {"health", 0x8000'0008U, 0U, 10U},
    };
    goldsrc::DeltaSchemaRegistryBuilder builder;
    std::vector<fixture::Field> entity_fields{std::begin(kEntityFields),std::end(kEntityFields)};
    std::vector<fixture::Field> client_fields{std::begin(kClientFields),std::end(kClientFields)};
    std::vector<fixture::Field> player_fields{std::begin(kEntityFields),std::end(kEntityFields)};
    if (model_field_type) { entity_fields.push_back({"modelindex",*model_field_type,600U,16U}); }
    if (brush_fields) {
        entity_fields.push_back({"solid",0x0000'0002U,604U,3U});
        entity_fields.push_back({"movetype",0x0000'0008U,608U,4U});
    }
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
    if (scripted_event_fields) {
        const auto encoded = fixture::schema("event_t", event_fixture::kFields);
        const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(encoded, 0U);
        REQUIRE(parsed);
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
    result.game_client = std::make_shared<hlclient::game_api::GameClientHost>(
        hlclient::games::halflife::make_half_life_client_module());
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

TEST_CASE("G1 selected module owns framed messages and rejects semantic suffix atomically",
          "[goldsrc][runtime-replay][game-module][weapon-hud]")
{
    auto init = initialization();
    init.user_message_definitions = {
        {91U, -1, "WeaponList"}, {88U, -1, "DeathMsg"},
        {93U, -1, "Health"}, {94U, -1, "Battery"}, {95U, 1, "ProjectUnknown"}};
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
    REQUIRE(session);
    std::vector<std::byte> wire{std::byte{93}, std::byte{1}, std::byte{77},
        std::byte{95}, std::byte{42}, std::byte{91}, std::byte{23}};
    for (const char character : std::string_view{"weapon_crowbar"})
        wire.push_back(std::byte{static_cast<std::uint8_t>(character)});
    for (const auto value : {0U,255U,255U,255U,255U,0U,0U,2U,0U,
                            88U,4U,0U,2U,120U,0U})
        wire.push_back(std::byte{static_cast<std::uint8_t>(value)});
    auto incoming = record(std::move(wire), 42U, 13001U, 1U);
    REQUIRE(session.session->apply_record(incoming));
    const auto committed = world.runtime_observation();
    REQUIRE(committed->weapon_hud.catalogue.size() == 1U);
    REQUIRE(committed->life_events.size() == 1U);
    CHECK(committed->weapon_hud.health == 77U);
    CHECK(committed->weapon_hud.revision == 2U); // unknown/notice do not revise HUD
    std::fill(incoming.payload.bytes.begin(), incoming.payload.bytes.end(), std::byte{0});
    CHECK(committed->weapon_hud.catalogue[0].command_name == "weapon_crowbar");
    CHECK(committed->life_events[0].weapon_text == "x");
    // Both messages are completely framed: rejection is inside the selected
    // game module, after a valid prefix, and must publish neither prefix nor effects.
    const auto bad = session.session->apply_record(record(
        {std::byte{93},std::byte{1},std::byte{66},std::byte{94},std::byte{1},std::byte{11}},
        43U,13002U,2U));
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error);
    CHECK(bad.error->code == goldsrc::RuntimeReplayErrorCode::semantic_value_mismatch);
    CHECK(world.runtime_observation() == committed);
    REQUIRE(session.session->apply_record(record(
        {std::byte{95},std::byte{17}},44U,13003U,3U)));
    CHECK(world.runtime_observation()->weapon_hud.health == 77U);
    CHECK(world.runtime_observation()->life_events.empty());
}

TEST_CASE("G1 protocol-only replay never silently selects Half-Life",
          "[goldsrc][runtime-replay][game-module]")
{
    auto generic = initialization();
    generic.game_client.reset();
    generic.user_message_definitions = {{91U,1,"Health"}};
    auto selected = initialization();
    selected.user_message_definitions = generic.user_message_definitions;
    client::ClientWorldState generic_world, game_world;
    auto generic_session = goldsrc::RuntimeReplaySession::initialize(generic, generic_world);
    auto game_session = goldsrc::RuntimeReplaySession::initialize(selected, game_world);
    REQUIRE(generic_session);
    REQUIRE(game_session);
    const auto incoming = record({std::byte{91},std::byte{77}},42U,13004U,1U);
    REQUIRE(generic_session.session->apply_record(incoming));
    REQUIRE(game_session.session->apply_record(incoming));
    CHECK_FALSE(generic_world.runtime_observation()->weapon_hud.health);
    CHECK(generic_world.runtime_observation()->weapon_hud.revision == 0U);
    CHECK(generic_world.runtime_observation()->lifecycle.life_epoch == 0U);
    CHECK(game_world.runtime_observation()->weapon_hud.health == 77U);
    CHECK(generic_world.runtime_observation()->canonical_state_hash ==
          game_world.runtime_observation()->canonical_state_hash);
}

TEST_CASE("C dynamic life messages are atomic owning one-shot events",
          "[goldsrc][runtime-replay][damage-respawn]") {
    for (const std::uint8_t damage_id : {std::uint8_t{91}, std::uint8_t{121}}) {
        auto init = initialization();
        init.receiving_player_entity = 1U;
        init.user_message_definitions = {{damage_id,12,"Damage"}, {88,-1,"DeathMsg"},
            {90,1,"ResetHUD"}, {89,0,"InitHUD"}, {92,1,"Health"}, {93,2,"Battery"}};
        client::ClientWorldState world;
        auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
        REQUIRE(session);
        REQUIRE(session.session->apply_record(record(initial_mixed(),100,1000,1)));
        CHECK(world.runtime_observation()->lifecycle.life_epoch == 1U);
        auto damage = std::vector<std::byte>{std::byte{damage_id},std::byte{7},std::byte{5},
            std::byte{0xff},std::byte{0xff},std::byte{0xff},std::byte{0xff},
            std::byte{0xf8},std::byte{0xff},std::byte{0},std::byte{0},std::byte{8},std::byte{0},
            std::byte{92},std::byte{95},std::byte{93},std::byte{33},std::byte{0}};
        auto event_record = record(damage,101,1001,2);
        REQUIRE(session.session->apply_record(event_record));
        const auto committed = world.runtime_observation();
        REQUIRE(committed->life_events.size() == 1);
        CHECK(committed->life_events[0].damage_bits == 0xffffffffU);
        CHECK(committed->life_events[0].damage_origin[0] == -1.0);
        CHECK(committed->lifecycle.damage_events == 1U);
        CHECK(committed->weapon_hud.health == 95U);
        CHECK(committed->receiving_client->health == 100.0); // no local subtraction
        CHECK(committed->weapon_hud.armor == 33);
        const auto duplicate = session.session->apply_record(event_record);
        REQUIRE_FALSE(duplicate); // existing record-identity policy rejects duplicates atomically
        REQUIRE(duplicate.error);
        CHECK(duplicate.error->code == goldsrc::RuntimeReplayErrorCode::duplicate_record);
        CHECK(world.runtime_observation() == committed);
        REQUIRE(session.session->apply_record(record(damage,102,1002,3)));
        CHECK(world.runtime_observation()->lifecycle.damage_events == 2U); // same bytes, new source
        const auto before_bad = world.runtime_observation();
        auto malformed = damage;
        malformed.insert(malformed.end(), {std::byte{88},std::byte{4},
            std::byte{0},std::byte{1},std::byte{'x'},std::byte{0},std::byte{92}});
        CHECK_FALSE(session.session->apply_record(record(malformed,103,1003,4)));
        CHECK(world.runtime_observation() == before_bad);
        for (std::size_t length = 1; length < 13U; ++length) {
            auto truncated = damage;
            truncated.resize(length);
            CHECK_FALSE(session.session->apply_record(record(truncated,104,2000+length,4)));
            CHECK(world.runtime_observation() == before_bad);
        }
        for (auto bytes : {std::vector<std::byte>{std::byte{88},std::byte{1},std::byte{0}},
                           std::vector<std::byte>{std::byte{88},std::byte{4},std::byte{0},
                               std::byte{1},std::byte{'x'},std::byte{'y'}}})
            CHECK_FALSE(session.session->apply_record(record(bytes,104,2100+bytes.size(),4)));
        REQUIRE(session.session->apply_record(record({std::byte{90},std::byte{0},std::byte{89}},105,1005,5)));
        CHECK(world.runtime_observation()->lifecycle.life_epoch == 1U);
        CHECK_FALSE(world.runtime_observation()->lifecycle.dead());
    }
}

TEST_CASE("C HLDM notice and fresh clientdata drive repeated lives without losing delta bases",
          "[goldsrc][runtime-replay][damage-respawn]") {
    auto init = initialization();
    init.receiving_player_entity = 1U;
    init.user_message_definitions = {{88,-1,"DeathMsg"}, {90,1,"ResetHUD"}, {89,0,"InitHUD"}};
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
    REQUIRE(session);
    REQUIRE(session.session->apply_record(record(initial_mixed(),100,3000,1)));
    std::uint32_t seq = 100;
    std::size_t ordinal = 1;
    const auto apply = [&](std::vector<std::byte> wire) {
        ++seq; ++ordinal;
        auto incoming = record(std::move(wire),seq,3000+ordinal,ordinal);
        incoming.payload.reassembled = false;
        const auto applied = session.session->apply_record(incoming);
        INFO((applied.error ? applied.error->context : ""));
        REQUIRE(applied);
    };
    // HP zero alone and other victim cannot classify this receiving player dead.
    apply(client_only(100,0));
    auto client_base = seq;
    CHECK_FALSE(world.runtime_observation()->lifecycle.dead());
    apply({std::byte{88},std::byte{4},std::byte{0},std::byte{2},std::byte{'x'},std::byte{0}});
    CHECK_FALSE(world.runtime_observation()->lifecycle.dead());
    for (std::uint64_t cycle = 1; cycle <= 3; ++cycle) {
        // World killer is legal; weapon text is bounded and inert.
        apply({std::byte{88},std::byte{4},std::byte{0},std::byte{1},std::byte{';'},std::byte{0}});
        const auto base_tag = static_cast<std::uint8_t>(client_base);
        apply(client_only(base_tag,0));
        REQUIRE(world.runtime_observation()->lifecycle.dead());
        CHECK(world.runtime_observation()->lifecycle.deaths == cycle);
        const auto dead_sequence = seq;
        apply({std::byte{90},std::byte{0}}); // reset before alive sample, not a reconnect
        CHECK(world.runtime_observation()->lifecycle.dead());
        apply(client_only(static_cast<std::uint8_t>(dead_sequence),80));
        client_base = seq;
        CHECK(world.runtime_observation()->lifecycle.life_epoch == cycle + 1U);
        CHECK(world.runtime_observation()->lifecycle.respawns == cycle);
        CHECK(world.runtime_observation()->generation == 1U);
        CHECK(world.runtime_observation()->packet_entities.size() == 3U);
        CHECK(world.runtime_observation()->receiving_client->view_offset.z == 28.0);
    }
}

TEST_CASE("Runtime replay commits dynamically registered Valve weapon HUD messages atomically",
          "[goldsrc][runtime-replay][weapon-hud]")
{
    auto init = initialization();
    init.user_message_definitions = {
        {91U, -1, "WeaponList"}, {83U, 3, "CurWeapon"},
        {77U, 2, "AmmoX"}, {88U, 2, "Battery"},
        {92U, 1, "ResetHUD"}};
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
    REQUIRE(session);
    std::vector<std::byte> wire;
    const auto add = [&](std::initializer_list<std::uint8_t> bytes) {
        for (const auto value : bytes) wire.push_back(std::byte{value});
    };
    add({83U, 1U, 2U, 8U}); // CurWeapon before its type catalogue.
    add({91U, 23U});
    for (const char character : std::string_view{"weapon_crowbar"})
        wire.push_back(std::byte{static_cast<std::uint8_t>(character)});
    add({0U, 0xffU, 0xffU, 0xffU, 0xffU, 0U, 0U, 2U, 0U});
    add({77U, 4U, 37U});
    add({88U, 72U, 0U});
    add({35U, 3U, 1U});
    const auto applied = session.session->apply_record(
        record(std::move(wire), 42U, 12'001U, 1U));
    REQUIRE(applied);
    REQUIRE(world.runtime_observation());
    const auto& state = *world.runtime_observation();
    CHECK(state.weapon_hud.active_weapon_id == 2U);
    REQUIRE(state.weapon_hud.catalogue.size() == 1U);
    CHECK(state.weapon_hud.catalogue[0].command_name == "weapon_crowbar");
    CHECK(state.weapon_hud.clips[2U] == 8);
    CHECK(state.weapon_hud.reserve_ammo[4U] == 37U);
    CHECK(state.weapon_hud.armor == 72);
    CHECK(state.weapon_hud.animation_sequence == 3U);
    CHECK(state.weapon_hud.animation_body == 1U);
    CHECK_FALSE(state.receiving_client);
    const auto typed_hash = client::runtime_observation_weapon_hud_hash(state);
    const auto canonical_hash = state.canonical_state_hash;

    auto invalid = session.session->apply_record(
        record({std::byte{88U},std::byte{5U},std::byte{0U},
                std::byte{83U},std::byte{1U}}, 43U, 12'002U, 2U));
    CHECK_FALSE(invalid);
    CHECK(world.runtime_observation()->canonical_state_hash == canonical_hash);
    CHECK(client::runtime_observation_weapon_hud_hash(
        *world.runtime_observation()) == typed_hash);

    auto cleared = session.session->apply_record(
        record({std::byte{83U},std::byte{0U},std::byte{0xffU},
                std::byte{0xffU}}, 44U, 12'003U, 3U));
    REQUIRE(cleared);
    CHECK(world.runtime_observation()->weapon_hud.active_weapon_id == 0U);
    CHECK(world.runtime_observation()->weapon_hud.catalogue.size() == 1U);
    CHECK(world.runtime_observation()->canonical_state_hash == canonical_hash);
    CHECK(client::runtime_observation_weapon_hud_hash(
        *world.runtime_observation()) != typed_hash);
    auto known_zero = session.session->apply_record(record(
        {std::byte{83U}, std::byte{1U}, std::byte{2U}, std::byte{0xffU},
         std::byte{77U}, std::byte{4U}, std::byte{0U},
         std::byte{88U}, std::byte{0U}, std::byte{0U}},
        45U, 12'004U, 4U));
    REQUIRE(known_zero);
    CHECK(world.runtime_observation()->weapon_hud.active_weapon_id == 2U);
    CHECK(world.runtime_observation()->weapon_hud.clips[2U] == -1);
    CHECK(world.runtime_observation()->weapon_hud.reserve_ammo[4U] == 0U);
    CHECK(world.runtime_observation()->weapon_hud.armor == 0);
    auto reset = session.session->apply_record(record(
        {std::byte{92U}, std::byte{0U}}, 46U, 12'005U, 5U));
    REQUIRE(reset);
    CHECK_FALSE(world.runtime_observation()->weapon_hud.active_weapon_id);
    CHECK_FALSE(world.runtime_observation()->weapon_hud.reserve_ammo[4U]);
    CHECK_FALSE(world.runtime_observation()->weapon_hud.armor);
    CHECK(world.runtime_observation()->weapon_hud.catalogue.size() == 1U);
}

TEST_CASE("Weapon selection requires current ownership and one safe catalogue token",
          "[goldsrc][runtime-replay][weapon-selection]")
{
    client::RuntimeClientObservationState state;
    state.receiving_client.emplace();
    state.receiving_client->owned_weapon_bits = (1U << 2U) | (1U << 5U);
    client::RuntimeWeaponTypeObservation crowbar;
    crowbar.id = 2U; crowbar.command_name = "weapon_crowbar";
    crowbar.slot = 0U; crowbar.source.record_identity = 1U;
    client::RuntimeWeaponTypeObservation pistol = crowbar;
    pistol.id = 5U; pistol.command_name = "weapon_9mmhandgun";
    pistol.slot = 1U;
    state.weapon_hud.catalogue = {crowbar, pistol};
    const auto built = hlclient::games::halflife::build_weapon_selection_request(state, 5U);
    REQUIRE(built);
    REQUIRE(built.bytes);
    CHECK(built.bytes->front() == std::byte{3U});
    CHECK(built.bytes->back() == std::byte{0U});
    CHECK(hlclient::games::halflife::select_owned_weapon_group(state, 1U, {}) == 2U);
    CHECK(hlclient::games::halflife::cycle_owned_weapon(state, 2U, 1) == 5U);
    state.receiving_client->owned_weapon_bits = 1U << 2U;
    CHECK_FALSE(hlclient::games::halflife::build_weapon_selection_request(state, 5U));
    state.weapon_hud.catalogue[0].command_name = "weapon_crowbar;quit";
    CHECK_FALSE(hlclient::games::halflife::build_weapon_selection_request(state, 2U));
}

TEST_CASE("Runtime decoder dynamically frames pickup records before the production game atomic commit",
          "[goldsrc][runtime-replay][pickups][game-module]") {
  for (const std::uint8_t pickup_id : {std::uint8_t{79U},std::uint8_t{107U}}) {
    auto init = initialization();
    init.user_message_definitions = {{pickup_id,2,"AmmoPickup"},{83U,2,"AmmoX"},
        {84U,-1,"ItemPickup"},{85U,1,"WeapPickup"},{86U,1,"Health"},
        {87U,-1,"ProjectUnknown"}};
    const auto host = init.game_client;
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init,world);
    REQUIRE(session);
    REQUIRE(session.session->apply_record(record(
        {std::byte{83U},std::byte{1U},std::byte{50U}},1U,1U,1U)));
    auto notification = record({std::byte{pickup_id},std::byte{1U},std::byte{17U}},2U,2U,2U);
    REQUIRE(session.session->apply_record(notification));
    CHECK(world.runtime_observation()->weapon_hud.reserve_ammo[1U] == 50U);
    auto hud = host->hud(*world.runtime_observation(),1.0);
    REQUIRE(hud.inventory_feedback.size() == 1U);
    CHECK(hud.inventory_feedback[0] == "PICKUP AMMO 1 +17");
    const auto revision = hud.inventory_feedback_revision;
    CHECK_FALSE(session.session->apply_record(notification)); // duplicate cannot reach module commit
    CHECK(host->hud(*world.runtime_observation(),1.1).inventory_feedback_revision == revision);
    REQUIRE(session.session->apply_record(record(
        {std::byte{83U},std::byte{1U},std::byte{67U}},3U,3U,3U)));
    CHECK(world.runtime_observation()->weapon_hud.reserve_ammo[1U] == 67U);
    CHECK(host->hud(*world.runtime_observation(),1.2).inventory_notifications_received == 1U);
    const auto committed = world.runtime_observation();
    std::vector<std::byte> suffix{std::byte{84U},std::byte{13U}};
    for (const auto c : std::string_view{"item_battery"})
      suffix.push_back(std::byte{static_cast<std::uint8_t>(c)});
    suffix.push_back(std::byte{0});
    suffix.push_back(std::byte{86U}); // missing fixed-size Health body
    const auto bad = session.session->apply_record(record(suffix,4U,4U,4U));
    CHECK_FALSE(bad); REQUIRE(bad.error);
    CHECK(bad.error->code == goldsrc::RuntimeReplayErrorCode::decoder_failed);
    CHECK(world.runtime_observation() == committed);
    CHECK(host->hud(*committed,1.3).inventory_notifications_received == 1U);
    suffix.pop_back();
    suffix[2U] = std::byte{';'}; // exact framing but invalid game body
    const auto semantic = session.session->apply_record(record(suffix,5U,5U,5U));
    CHECK_FALSE(semantic); REQUIRE(semantic.error);
    CHECK(semantic.error->code == goldsrc::RuntimeReplayErrorCode::semantic_value_mismatch);
    CHECK(world.runtime_observation() == committed);
    suffix[2U] = std::byte{'i'};
    REQUIRE(session.session->apply_record(record(suffix,6U,6U,6U)));
    hud = host->hud(*world.runtime_observation(),1.4);
    CHECK(hud.inventory_notifications_received == 2U);
    CHECK(hud.inventory_feedback.back() == "PICKUP BATTERY");
    REQUIRE(session.session->apply_record(record(
        {std::byte{87U},std::byte{2U},std::byte{42U},std::byte{43U}},7U,7U,7U)));
    CHECK(host->hud(*world.runtime_observation(),1.5).inventory_notifications_received == 2U);
    // Rejected suffixes do not destroy the independent client/entity delta histories.
    REQUIRE(session.session->apply_record(record(client_no_base_recovery(),8U,8U,8U)));
    CHECK(world.runtime_observation()->receiving_client->health == 60.0);
    CHECK(host->hud(*world.runtime_observation(),1.6).inventory_notifications_received == 2U);
  }
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

TEST_CASE("E10 exact advertised player animation fields publish atomically",
          "[e10][goldsrc][runtime-replay]")
{
    auto init = initialization(1U,4096U,false,true);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    for (const auto name : {"entity_state_t","clientdata_t","weapon_data_t"})
        REQUIRE(builder.insert(*init.schemas->find_exact(name)));
    std::vector<fixture::Field> fields{std::begin(kEntityFields),std::end(kEntityFields)};
    fields.push_back({"animtime",0x0000'0020U,24U,8U});
    fields.push_back({"framerate",0x8000'0004U,28U,8U,64'000U,4'000U});
    fields.push_back({"gaitsequence",0x0000'0008U,32U,8U});
    fields.push_back({"weaponmodel",0x0000'0008U,36U,10U});
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(fixture::schema("entity_state_player_t",fields),0U);
    REQUIRE(parsed); REQUIRE(builder.insert(*parsed.schema));
    init.schemas = std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(builder).publish());
    init.baselines = make_baselines(*init.schemas,1U,true);
    client::ClientWorldState world;
    const auto session = goldsrc::RuntimeReplaySession::initialize(init,world);
    INFO((session.error ? session.error->context : "initialized")); REQUIRE(session);
    fixture::BitWriter writer; time_prefix(writer,100.0F);
    entity_header(writer,goldsrc::kGoldSrcSvcPacketEntitiesOpcode,1U);
    writer.write(0U,1U); writer.write(0U,1U); writer.write(1U,6U);
    writer.write(0U,1U); writer.write(0U,1U);
    writer.write(2U,3U); writer.write(0xc0U,8U); writer.write(0x03U,8U);
    writer.write(25U,8U); writer.write(goldsrc_signed(24U),8U);
    writer.write(3U,8U); writer.write(9U,10U); finish_entities(writer);
    const auto applied=session.session->apply_record(record(writer.bytes(),100U,10001U,1U));
    INFO((applied.error ? applied.error->context : "applied")); REQUIRE(applied);
    const auto published=world.runtime_observation(); REQUIRE(published->packet_entities.size()==1U);
    const auto& player=published->packet_entities.front();
    CHECK(player.player_movement_schema); CHECK(player.ordinary_visual_schema);
    REQUIRE(player.animation_time_seconds); CHECK(*player.animation_time_seconds==Catch::Approx(99.75));
    REQUIRE(player.frame_rate); CHECK(*player.frame_rate==Catch::Approx(1.5));
    CHECK(player.gait_sequence==3U); CHECK(player.weapon_model_index==9U);
    CHECK_FALSE(player.velocity.x); CHECK_FALSE(player.velocity.y); CHECK_FALSE(player.velocity.z);
    auto changed=*published; changed.packet_entities.front().gait_sequence=4U;
    CHECK(client::runtime_observation_canonical_hash(changed)==published->canonical_state_hash);
    CHECK(client::runtime_observation_visual_hash(changed)==client::runtime_observation_visual_hash(*published));
    CHECK(client::runtime_observation_visual_hash_v2(changed)!=client::runtime_observation_visual_hash_v2(*published));
    auto malformed=writer.bytes(); malformed.push_back(std::byte{255});
    REQUIRE_FALSE(session.session->apply_record(record(malformed,101U,10002U,2U)));
    CHECK(world.runtime_observation()==published);
}

TEST_CASE("E10 absent animation descriptors remain unavailable", "[e10][goldsrc][runtime-replay]")
{
    client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(initialization(),world);
    REQUIRE(session); REQUIRE(session.session->apply_record(record(initial_mixed(),100U,10011U,1U)));
    for(const auto& entity:world.runtime_observation()->packet_entities) {
        CHECK_FALSE(entity.animation_time_seconds); CHECK_FALSE(entity.frame_rate);
        CHECK_FALSE(entity.gait_sequence); CHECK_FALSE(entity.weapon_model_index); CHECK_FALSE(entity.velocity.complete());
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

TEST_CASE("D4 ordinary entity solid and movetype publish atomically",
          "[d4][goldsrc][runtime-replay]")
{
    auto init=initialization();
    init.schemas=make_schemas(false,{},false,true);
    init.baselines=make_baselines(*init.schemas,1U);
    client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(init,world);
    REQUIRE(session);
    fixture::BitWriter writer;
    time_prefix(writer,100.0F);
    entity_header(writer,goldsrc::kGoldSrcSvcPacketEntitiesOpcode,1U);
    writer.write(0U,1U); writer.write(0U,1U); writer.write(2U,6U);
    writer.write(0U,1U); writer.write(0U,1U);
    const std::pair<std::uint32_t,std::size_t> values[]{{4U,3U},{7U,4U}};
    delta(writer,0xc0U,values); finish_entities(writer);
    auto input=record(writer.bytes(),100U,997U,1U);
    const auto applied=session.session->apply_record(input);
    REQUIRE(applied);
    const auto published=world.runtime_observation();
    REQUIRE(published->packet_entities.size()==1U);
    CHECK(published->packet_entities.front().solid==4U);
    CHECK(published->packet_entities.front().brush_move_type==7U);
    CHECK_FALSE(published->packet_entities.front().player_movement_schema);
    auto malformed=writer.bytes(); malformed.push_back(std::byte{255});
    CHECK_FALSE(session.session->apply_record(record(malformed,101U,998U,2U)));
    CHECK(world.runtime_observation()==published);
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


TEST_CASE("Owning runtime failure survives rollback payload release and terminal formatting",
          "[runtime-diagnostics][goldsrc][runtime-replay][game-module]") {
    auto init = initialization();
    init.user_message_definitions = {{79U,2,"AmmoPickup"},{83U,2,"AmmoX"},
        {84U,-1,"ItemPickup"},{86U,1,"Health"}};
    const auto host = init.game_client;
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init,world);
    REQUIRE(session);
    REQUIRE(session.session->apply_record(record(
        {std::byte{83U},std::byte{1U},std::byte{50U}},90U,901U,1U)));
    REQUIRE(session.session->apply_record(record(
        {std::byte{86U},std::byte{90U},std::byte{79U},std::byte{1U},std::byte{17U}},91U,902U,2U)));
    const auto committed = world.runtime_observation();
    const auto hud = host->hud(*committed,1.0);
    REQUIRE(hud.inventory_notifications_received == 1U);
    REQUIRE(world.runtime_publication_revision() == 3U);
    goldsrc::RuntimeReplayError retained;
    SECTION("unsupported suffix at decoder-established boundary") {
        {
            auto bad = record({std::byte{86U},std::byte{33U},std::byte{79U},
                std::byte{1U},std::byte{4U},std::byte{255U}},92U,903U,3U);
            bad.payload.source_acknowledgement = 91U;
            bad.payload.source_reliable = true; bad.payload.reassembled = true;
            auto result = session.session->apply_record(bad);
            REQUIRE_FALSE(result); REQUIRE(result.error);
            retained = std::move(*result.error);
        } // body, candidate and result no longer exist
        CHECK(retained.code == goldsrc::RuntimeReplayErrorCode::decoder_failed);
        REQUIRE(retained.control_error);
        CHECK(retained.control_error->code == goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
        REQUIRE(retained.decoder_cursor); REQUIRE(retained.failure_cursor);
        CHECK(retained.decoder_cursor->byte_offset() == 5U);
        CHECK(retained.failure_cursor->byte_offset() == 6U);
        REQUIRE(retained.decoder_wire_opcode);
        CHECK(*retained.decoder_wire_opcode == std::uint8_t{255U});
        CHECK(retained.source_acknowledgement == 91U);
        CHECK(retained.reliable == true); CHECK(retained.reassembled == true);
        const auto summary = goldsrc::runtime_failure_summary(retained);
        CHECK(summary.find("control_error=unsupported_opcode") != std::string::npos);
    }
    SECTION("truncated registered body") {
        auto result = session.session->apply_record(record({std::byte{86U}},92U,903U,3U));
        REQUIRE_FALSE(result); REQUIRE(result.error); retained = std::move(*result.error);
        REQUIRE(retained.control_error);
        CHECK(retained.control_error->code == goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
        CHECK(retained.control_error->registration_name == "Health");
        REQUIRE(retained.control_error->registration_id);
        CHECK(*retained.control_error->registration_id == std::uint8_t{86U});
        CHECK(retained.control_error->expected_body_size == 1U);
        CHECK(retained.control_error->actual_body_size == 0U);
        REQUIRE(retained.failure_cursor); CHECK(retained.failure_cursor->byte_offset() == 1U);
    }
    SECTION("game module semantic failure after valid prefix") {
        auto result = session.session->apply_record(record(
            {std::byte{86U},std::byte{12U},std::byte{84U},std::byte{2U},std::byte{';'},std::byte{0U}},92U,903U,3U));
        REQUIRE_FALSE(result); REQUIRE(result.error); retained = std::move(*result.error);
        CHECK(retained.code == goldsrc::RuntimeReplayErrorCode::semantic_value_mismatch);
        REQUIRE(retained.module_error);
        CHECK(retained.module_error->code == hlclient::game_api::GameMessageErrorCode::invalid_token);
        CHECK(retained.module_error->message_name == "ItemPickup");
        REQUIRE(retained.module_error->registration_id);
        CHECK(*retained.module_error->registration_id == std::uint8_t{84U});
        CHECK(retained.module_error->actual_body_size == 2U);
        REQUIRE(retained.decoder_cursor); CHECK(retained.decoder_cursor->byte_offset() == 2U);
        REQUIRE(retained.failure_cursor); CHECK(retained.failure_cursor->byte_offset() == 4U);
        const auto summary = goldsrc::runtime_failure_summary(retained);
        CHECK(summary.find("module_error=invalid_token") != std::string::npos);
        CHECK(summary.find("user_message_name=ItemPickup") != std::string::npos);
        CHECK(summary.find(";") == std::string::npos);
    }
    CHECK(world.runtime_observation() == committed);
    CHECK(world.runtime_publication_revision() == 3U);
    CHECK(host->hud(*committed,1.1).inventory_feedback == hud.inventory_feedback);
    CHECK(host->hud(*committed,1.1).inventory_notifications_received == 1U);
    CHECK(retained.record_identity == 903U); CHECK(retained.record_ordinal == 3U);
    CHECK(retained.generation == 1U); CHECK(retained.source_sequence == 92U);
    CHECK(retained.last_publication == 3U);
    CHECK(retained.attempted_records == 3U); CHECK(retained.committed_records == 2U);
    // Offline handoff uses the production formatter and actual decoder/handler
    // result, never a handwritten copy of nested diagnostic values.
    std::cout << "live_application_outcome result=error primary_error=runtime_record_failed"
        << " runtime_error=" << goldsrc::to_string(retained.code)
        << " parser_error=" << (retained.decoder_error ? goldsrc::to_string(*retained.decoder_error) : "unavailable")
        << " opcode=" << (retained.decoder_wire_opcode ? std::to_string(*retained.decoder_wire_opcode) : "unavailable")
        << " cursor=" << (retained.decoder_cursor ? std::to_string(retained.decoder_cursor->absolute_bit_offset()) : "unavailable")
        << " record=3 source_sequence=92 scripted_coverage=not_evaluated prediction_coverage=limited"
           " inventory_notifications=1 feedback_rows=1"
           " use_press=1 use_release=1 use_generated=5 use_transmitted=5 use_clear_transmitted=1 use_sent=1"
           " use_health_before=unavailable use_health_after=90 use_armor_before=unavailable use_armor_after=unavailable"
           " use_server_effect=not_observed use_reason=unavailable use_prediction_state=suspended use_prediction_reason=runtime_record_failed"
        << goldsrc::runtime_failure_summary(retained) << '\n';
    session.session->finish();
    CHECK(goldsrc::runtime_failure_summary(retained).find("last_publication=3") != std::string::npos);
}

TEST_CASE("Scripted event framing crosses mixed production replay without gameplay or audio side effects",
          "[goldsrc][runtime-replay][scripted-events][e9-fix]")
{
    auto init = initialization();
    init.schemas = make_schemas(false, std::nullopt, false, false, true);
    init.baselines = make_baselines(*init.schemas, init.generation);
    auto queue = std::make_shared<goldsrc::CommittedSoundQueue>();
    init.sound_events = queue;
    client::ClientWorldState world, reference_world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
    auto reference_init = init;
    reference_init.sound_events.reset();
    // Different instances of the same selected module: no shared mutable
    // game state and no second production runtime implementation.
    reference_init.game_client = std::make_shared<hlclient::game_api::GameClientHost>(
        hlclient::games::halflife::make_half_life_client_module());
    auto reference = goldsrc::RuntimeReplaySession::initialize(reference_init, reference_world);
    REQUIRE(session); REQUIRE(reference);
    auto neutral_bytes = initial_mixed();
    neutral_bytes.insert(neutral_bytes.end(), event_fixture::kSound.begin(), event_fixture::kSound.end());
    REQUIRE(reference.session->apply_record(record(neutral_bytes, 100U, 40'001U, 1U)));

    auto bytes = initial_mixed();
    bytes.insert(bytes.end(), event_fixture::kMultiple.begin(), event_fixture::kMultiple.end());
    bytes.insert(bytes.end(), event_fixture::kReliableDelta.begin(), event_fixture::kReliableDelta.end());
    bytes.insert(bytes.end(), event_fixture::kSound.begin(), event_fixture::kSound.end());
    auto incoming = record(std::move(bytes), 100U, 40'001U, 1U);
    auto applied = session.session->apply_record(incoming);
    REQUIRE(applied);
    CHECK(applied.event->clientdata_observed);
    CHECK(applied.event->entities_observed);
    CHECK(applied.event->canonical_state_hash == reference_world.runtime_observation()->canonical_state_hash);
    CHECK(world.runtime_observation()->weapon_slots[2].clip == 8);
    CHECK(world.runtime_observation()->receiving_client->health == 100.0);
    CHECK(queue->starts == 1U); // only actual svc_sound, not scripted event args
    goldsrc::CommittedSound sound;
    REQUIRE(queue->pop(sound));
    CHECK(sound.opcode == goldsrc::RuntimeControlOpcode::svc_sound);
    CHECK(sound.sound.entity_reference == 19U);
    CHECK(sound.sound.sound_reference == 5U);
    CHECK_FALSE(queue->pop(sound));
    std::vector<const goldsrc::RuntimeControlScriptedEvents*> bodies;
    for (const auto& event : applied.event->decoded_batch.events)
        if (const auto* control = std::get_if<goldsrc::RuntimeControlEvent>(&event))
            if (const auto* body = std::get_if<goldsrc::RuntimeControlScriptedEvents>(&control->body))
                bodies.push_back(body);
    REQUIRE(bodies.size() == 2U);
    REQUIRE(bodies[0]->entries.size() == 3U);
    REQUIRE(bodies[1]->entries.size() == 1U);
    CHECK(bodies[0]->entries[1].event_index == 511U);
    CHECK(bodies[1]->reliable);
    const auto duplicate = session.session->apply_record(incoming);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error->code == goldsrc::RuntimeReplayErrorCode::duplicate_record);
    CHECK(queue->starts == 1U);
    CHECK_FALSE(queue->pop(sound));
    incoming.payload.bytes.clear();
    const auto* retained = bodies[1]->entries[0].arguments->find_exact("fixture_value");
    REQUIRE(retained);
    CHECK(std::get<std::uint32_t>(retained->value()) == 42U);

    auto next_bytes = mixed_delta();
    next_bytes.insert(next_bytes.end(), event_fixture::kReliableDefault.begin(), event_fixture::kReliableDefault.end());
    auto next_reference_bytes = mixed_delta();
    next_reference_bytes.push_back(std::byte{1});
    REQUIRE(reference.session->apply_record(record(std::move(next_reference_bytes), 101U, 40'002U, 2U)));
    REQUIRE(session.session->apply_record(record(std::move(next_bytes), 101U, 40'002U, 2U)));
    CHECK(world.runtime_observation()->canonical_state_hash == reference_world.runtime_observation()->canonical_state_hash);
    CHECK(world.runtime_observation()->receiving_client->health == 75.0);
    CHECK(world.runtime_observation()->weapon_slots[2].clip == 5);
    CHECK(queue->starts == 1U);
    CHECK_FALSE(queue->pop(sound));

    const auto committed = world.runtime_observation();
    const auto history = session.session->decoder_state().history().snapshot_count();
    auto invalid = client_only(101U, 66U);
    invalid.insert(invalid.end(), event_fixture::kPacketDelta.begin(), event_fixture::kPacketDelta.end());
    invalid.insert(invalid.end(), event_fixture::kSound.begin(), event_fixture::kSound.end());
    invalid.push_back(std::byte{255});
    const auto rejected = session.session->apply_record(record(std::move(invalid), 102U, 40'003U, 3U));
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error->code == goldsrc::RuntimeReplayErrorCode::decoder_failed);
    CHECK(world.runtime_observation() == committed);
    CHECK(session.session->decoder_state().history().snapshot_count() == history);
    CHECK(queue->starts == 1U);
    CHECK_FALSE(queue->pop(sound));
    CHECK(committed->receiving_client->health == 75.0);
    session.session->finish();
    const auto finished = session.session->apply_record(record(
        event_fixture::bytes(event_fixture::kReliableDelta), 103U, 40'004U, 4U));
    REQUIRE_FALSE(finished);
    CHECK(finished.error->code == goldsrc::RuntimeReplayErrorCode::session_finished);
    CHECK_FALSE(queue->pop(sound));
}

TEST_CASE("Metadata-shaped synthetic opcode3 suffix stays alive at the reported exact cursor",
          "[goldsrc][runtime-replay][scripted-events][literal][e9-fix]")
{
    // Not reconstructed stock bytes: the failed manual payload was not saved.
    // Match its reported geometry only, using independently owned valid bytes.
    auto bytes = std::vector<std::byte>(156U, std::byte{1});
    bytes.insert(bytes.end(), event_fixture::kPacketNoDelta.begin(), event_fixture::kPacketNoDelta.end());
    const std::array suffix{std::byte{7}, std::byte{0}, std::byte{0},
        std::byte{128}, std::byte{63}, std::byte{1}};
    bytes.insert(bytes.end(), suffix.begin(), suffix.end());
    REQUIRE(bytes.size() == 167U);
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(initialization(), world);
    REQUIRE(session);
    const auto incoming = record(bytes, 504U, 489U, 489U);
    const auto result = session.session->apply_record(incoming);
    REQUIRE(result);
    REQUIRE(result.event->decoded_batch.events.size() == 159U);
    const auto* control = std::get_if<goldsrc::RuntimeControlEvent>(&result.event->decoded_batch.events[156U]);
    REQUIRE(control);
    CHECK(control->opcode == goldsrc::RuntimeControlOpcode::svc_event);
    CHECK(control->provenance.start_cursor.byte_offset() == 156U);
    CHECK(control->provenance.end_cursor.byte_offset() == 161U);
    CHECK(session.session->status() == goldsrc::RuntimeReplaySessionStatus::active);
    CHECK(world.runtime_observation()->server_time_seconds == 1.0);

    auto truncated = event_fixture::bytes(event_fixture::kPacketDelay);
    truncated.pop_back();
    const auto committed = world.runtime_observation();
    const auto bad = session.session->apply_record(record(std::move(truncated), 505U, 490U, 490U));
    REQUIRE_FALSE(bad);
    CHECK(bad.error->code == goldsrc::RuntimeReplayErrorCode::decoder_failed);
    CHECK(world.runtime_observation() == committed);
    REQUIRE(bad.error->control_error);
    CHECK(bad.error->control_error->code == goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
}

TEST_CASE("Mixed replay enforces scripted argument schema payload budget and generation boundaries",
          "[goldsrc][runtime-replay][scripted-events][limits][e9-fix]")
{
    auto init = initialization();
    auto queue = std::make_shared<goldsrc::CommittedSoundQueue>();
    init.sound_events = queue;
    SECTION("missing advertised schema rejects event and sound after valid clientdata") {
        client::ClientWorldState world;
        auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
        REQUIRE(session);
        const auto before = world.runtime_observation();
        auto incoming = initial_mixed();
        incoming.insert(incoming.end(), event_fixture::kReliableDelta.begin(), event_fixture::kReliableDelta.end());
        incoming.insert(incoming.end(), event_fixture::kSound.begin(), event_fixture::kSound.end());
        const auto bad = session.session->apply_record(record(std::move(incoming), 100U, 60'001U, 1U));
        REQUIRE_FALSE(bad);
        REQUIRE(bad.error->control_error);
        CHECK(bad.error->control_error->code == goldsrc::RuntimeControlDecodeErrorCode::missing_event_schema);
        CHECK(world.runtime_observation() == before);
        CHECK(session.session->decoder_state().history().snapshot_count() == 0U);
        CHECK(queue->starts == 0U);
        goldsrc::CommittedSound sound;
        CHECK_FALSE(queue->pop(sound));
    }
    SECTION("whole mixed payload event budget is checked before any commit") {
        init.schemas = make_schemas(false, std::nullopt, false, false, true);
        init.baselines = make_baselines(*init.schemas, init.generation);
        init.limits.decoder.controls.maximum_scripted_events_per_payload = 5U;
        client::ClientWorldState world;
        auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
        REQUIRE(session);
        const auto before = world.runtime_observation();
        auto incoming = initial_mixed();
        for (std::size_t count = 0U; count < 2U; ++count)
            incoming.insert(incoming.end(), event_fixture::kMultiple.begin(), event_fixture::kMultiple.end());
        incoming.insert(incoming.end(), event_fixture::kSound.begin(), event_fixture::kSound.end());
        const auto bad = session.session->apply_record(record(std::move(incoming), 100U, 60'001U, 1U));
        REQUIRE_FALSE(bad);
        REQUIRE(bad.error->control_error);
        CHECK(bad.error->control_error->code == goldsrc::RuntimeControlDecodeErrorCode::scripted_event_limit_exceeded);
        CHECK(world.runtime_observation() == before);
        CHECK(session.session->decoder_state().history().snapshot_count() == 0U);
        CHECK(queue->starts == 0U);
    }
    SECTION("generation reset retains returned owning values but rejects late old records") {
        init.schemas = make_schemas(false, std::nullopt, false, false, true);
        init.baselines = make_baselines(*init.schemas, init.generation);
        client::ClientWorldState world;
        auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
        REQUIRE(session);
        const auto old_record = record(event_fixture::bytes(event_fixture::kReliableDelta), 100U, 60'001U, 1U);
        const auto applied = session.session->apply_record(old_record);
        REQUIRE(applied);
        const auto* control = std::get_if<goldsrc::RuntimeControlEvent>(&applied.event->decoded_batch.events[0]);
        REQUIRE(control);
        const auto& old_body = std::get<goldsrc::RuntimeControlScriptedEvents>(control->body);
        auto reset = initialization(2U);
        reset.schemas = init.schemas;
        reset.baselines = make_baselines(*reset.schemas, 2U);
        reset.sound_events = queue;
        REQUIRE_FALSE(session.session->reset_generation(reset));
        CHECK(session.session->generation() == 2U);
        const auto late = session.session->apply_record(old_record);
        REQUIRE_FALSE(late);
        CHECK(late.error->code == goldsrc::RuntimeReplayErrorCode::generation_mismatch);
        CHECK(queue->starts == 0U);
        const auto* retained = old_body.entries[0].arguments->find_exact("fixture_value");
        REQUIRE(retained);
        CHECK(std::get<std::uint32_t>(retained->value()) == 42U);
        const auto fresh = session.session->apply_record(record(
            event_fixture::bytes(event_fixture::kReliableDefault), 101U, 60'002U, 1U, 2U));
        REQUIRE(fresh);
        const auto& fresh_body = std::get<goldsrc::RuntimeControlScriptedEvents>(
            std::get<goldsrc::RuntimeControlEvent>(fresh.event->decoded_batch.events[0]).body);
        CHECK(std::get<std::uint32_t>(fresh_body.entries[0].arguments->find_exact("fixture_value")->value()) == 0U);
        CHECK(queue->starts == 0U);
    }
}

} // namespace
