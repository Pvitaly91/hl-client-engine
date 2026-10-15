#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <hlclient/games/halflife/inventory.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>

#include "delta_test_fixture.hpp"
#include "event_test_fixture.hpp"
#include "crossbow_test_fixture.hpp"
#include "snark_sound_pop_test_fixture.hpp"

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
    const bool scripted_event_fields = false,
    const bool special_reload_fields = false,
    const std::optional<std::uint32_t> special_reload_field_type = std::nullopt,
    const bool pump_field = true,
    const std::optional<std::uint32_t> fov_type = std::nullopt,
    const std::optional<std::uint32_t> auxiliary_type = std::nullopt,
    const std::optional<std::uint32_t> physics_type = std::nullopt)
{
    const fixture::Field mismatched_client[]{
        {"health", 0x8000'0008U, 0U, 10U},
    };
    goldsrc::DeltaSchemaRegistryBuilder builder;
    std::vector<fixture::Field> entity_fields{std::begin(kEntityFields),std::end(kEntityFields)};
    std::vector<fixture::Field> client_fields{std::begin(kClientFields),std::end(kClientFields)};
    std::vector<fixture::Field> player_fields{std::begin(kEntityFields),std::end(kEntityFields)};
    std::vector<fixture::Field> weapon_fields{std::begin(kWeaponFields),std::end(kWeaponFields)};
    if(fov_type) client_fields.push_back({"fov",*fov_type,56U,8U});
    // DT_STRING carries a one-bit descriptor width; the value itself is the
    // bounded NUL-terminated byte string. Integer negative control stays8.
    if(physics_type) client_fields.push_back({"physinfo",*physics_type,900U,
        static_cast<std::uint8_t>(*physics_type == 0x80U ? 1U : 8U)});
    if(auxiliary_type) {
        client_fields.push_back({"vuser2[1]",*auxiliary_type,800U,10U});
        client_fields.push_back({"vuser2[2]",*auxiliary_type,804U,10U});
    }
    if (special_reload_fields) {
        // Negotiated unsigned DT_INTEGER, not a bool or signed ABI member.
        weapon_fields.push_back({"m_fInSpecialReload",
            special_reload_field_type.value_or(0x0000'0008U),16U,2U});
        if (pump_field) weapon_fields.push_back({"m_flPumpTime",
            0x8000'0004U,20U,22U,4'000'000U,4'000U});
    }
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
             fixture::schema("weapon_data_t", weapon_fields)}) {
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
    const bool include_player = false,
    const std::uint32_t max_clients = 1U)
{
    auto source = payload(baseline_bytes(include_player), 17U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    REQUIRE(cursor);
    const auto decoded = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        goldsrc::EntityBaselineDecodeInput{
            &source, *cursor, 1U, generation, max_clients,
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

void special_weapon_fields(
    fixture::BitWriter& writer,
    const std::uint8_t mask,
    const std::span<const std::pair<std::uint32_t, std::size_t>> values,
    const std::optional<std::uint8_t> base_tag = std::nullopt)
{
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode,8U);
    writer.write(base_tag ? 1U : 0U,1U);
    if (base_tag) writer.write(*base_tag,8U);
    delta(writer,0U); // Unrelated client values are zero/inherited.
    writer.write(1U,1U);
    writer.write(2U,6U); // Arbitrary wire slot: neutral field projection.
    delta(writer,mask,values);
    writer.write(0U,1U);
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
    client_delta(writer, 100U, 75U, std::uint8_t{5});
    entity_header(
        writer, goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U, std::uint8_t{100});
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

TEST_CASE("E14 replay exact registered bindings names receipt clock and rollback", "[e14][runtime-replay]") {
    namespace api = hlclient::game_api;
    auto init = initialization();init.max_clients=2U;init.receiving_player_entity=1U;
    init.user_message_definitions={{201U,9,"ScoreInfo"},{202U,-1,"DeathMsg"}};
    auto host=init.game_client;host->set_movement_audio_time_origin(1000.0);
    client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(std::move(init),world);REQUIRE(session);
    const auto append_info=[](std::vector<std::byte>& bytes,std::uint8_t index,std::uint32_t uid,std::string_view name) {
        bytes.push_back(std::byte{13});bytes.push_back(static_cast<std::byte>(index));
        for(unsigned i=0;i<4U;++i)bytes.push_back(static_cast<std::byte>((uid>>(8U*i))&255U));
        const auto info=uid ? "\\name\\"+std::string{name}+"\\private\\secret" : std::string{};
        for(const unsigned char c:info)bytes.push_back(static_cast<std::byte>(c));
        bytes.push_back(std::byte{0});bytes.resize(bytes.size()+16U,std::byte{0});
    };
    const auto append_score=[](std::vector<std::byte>& bytes,std::uint8_t slot,std::uint16_t frags,std::uint16_t deaths) {
        bytes.insert(bytes.end(),{std::byte{201},static_cast<std::byte>(slot),static_cast<std::byte>(frags&255U),static_cast<std::byte>(frags>>8U),
            static_cast<std::byte>(deaths&255U),static_cast<std::byte>(deaths>>8U),std::byte{0},std::byte{0},std::byte{0},std::byte{0}});
    };
    const auto hud_text=[&](std::string_view expected,bool tab=true,double seconds=1.0) {
        host->set_hud_context({640U,480U,tab});const auto hud=host->hud(*world.runtime_observation(),seconds);
        return std::any_of(hud.draw.texts.begin(),hud.draw.texts.end(),[&](const auto& t){return t.text.find(expected)!=std::string::npos;});
    };
    std::vector<std::byte> wire;append_info(wire,0U,11U,"Alice");append_info(wire,1U,12U,"Bob");
    append_score(wire,1U,0U,0U);append_score(wire,2U,0U,0U);
    auto first=record(wire,100U,13001U,1U);REQUIRE(session.session->apply_record(first));
    CHECK(hud_text("Alice"));CHECK(hud_text("Bob"));const auto canonical=world.runtime_observation()->canonical_state_hash;
    wire={std::byte{202},std::byte{13},std::byte{1},std::byte{2}};
    for(const char c:std::string{"9mmhandgun"})wire.push_back(static_cast<std::byte>(c));wire.push_back(std::byte{0});
    auto shot=record(wire,101U,13002U,2U);
    shot.received_at=goldsrc::SoundTime{std::chrono::duration_cast<goldsrc::SoundTime::duration>(std::chrono::duration<double>{1001.0})};
    REQUIRE(session.session->apply_record(shot));CHECK(hud_text("Alice [9mmhandgun] Bob",false));
    CHECK(world.runtime_observation()->canonical_state_hash==canonical);
    CHECK_FALSE(session.session->apply_record(shot));
    wire.clear();append_info(wire,1U,12U,"Rename");wire.push_back(std::byte{255});
    CHECK_FALSE(session.session->apply_record(record(wire,102U,13003U,3U)));
    CHECK(hud_text("Bob"));CHECK_FALSE(hud_text("Rename"));
    wire.pop_back();REQUIRE(session.session->apply_record(record(wire,102U,13003U,3U)));
    CHECK(hud_text("Rename"));CHECK(hud_text("Alice [9mmhandgun] Bob",false));
    wire.clear();append_info(wire,1U,0U,"");REQUIRE(session.session->apply_record(record(wire,103U,13004U,4U)));
    CHECK_FALSE(hud_text("Rename"));CHECK(hud_text("Alice [9mmhandgun] Bob",false));
    wire.clear();append_info(wire,1U,13U,"Eve");REQUIRE(session.session->apply_record(record(wire,104U,13005U,5U)));
    CHECK(hud_text("Eve"));CHECK_FALSE(hud_text("Alice [9mmhandgun] Bob",false,7.0));
    auto next=initialization(2U);next.max_clients=2U;next.receiving_player_entity=1U;next.game_client=host;
    REQUIRE_FALSE(session.session->reset_generation(std::move(next)));CHECK_FALSE(hud_text("Alice"));CHECK_FALSE(hud_text("Eve"));
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
    init.max_clients = 2U; // Fixture DeathMsg addresses player two.
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

TEST_CASE("E13 hit body and health death messages stay active on selected and protocol-only receivers",
          "[e13-hit][goldsrc][runtime-replay][damage-respawn]") {
    for(const bool selected:{false,true}) {
        auto init=initialization();init.max_clients=2U;
        init.baselines=make_baselines(*init.schemas,init.generation,false,2U);
        if(!selected) init.game_client.reset();
        init.receiving_player_entity=1;
        init.user_message_definitions={{86,1,"Health"},{88,-1,"DeathMsg"}};
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        const auto initial=session.session->apply_record(record(initial_mixed(),100,13001,1));
        INFO((initial.error ? initial.error->context : ""));REQUIRE(initial);
        const std::vector blood{std::byte{23},std::byte{115},std::byte{8},std::byte{0},
            std::byte{240},std::byte{255},std::byte{24},std::byte{0},std::byte{129},std::byte{0},
            std::byte{130},std::byte{0},std::byte{247},std::byte{3}};
        auto wire=blood;wire.insert(wire.end(),{std::byte{86},std::byte{38}});
        const auto hit=session.session->apply_record(record(wire,101,13002,2));REQUIRE(hit);
        REQUIRE(hit.event->decoded_batch.events.size()==2U);
        CHECK(session.session->status()==goldsrc::RuntimeReplaySessionStatus::active);
        if(selected) CHECK(world.runtime_observation()->weapon_hud.health==38U);
        const auto before=world.runtime_observation();
        wire.push_back(std::byte{254});
        REQUIRE_FALSE(session.session->apply_record(record(wire,102,13003,3)));
        CHECK(world.runtime_observation()==before);
        CHECK(session.session->status()==goldsrc::RuntimeReplaySessionStatus::active);
        auto death=blood;death.insert(death.end(),{std::byte{86},std::byte{0},std::byte{88},
            std::byte{4},std::byte{2},std::byte{1},std::byte{120},std::byte{0}});
        REQUIRE(session.session->apply_record(record(death,103,13004,3)));
        CHECK(session.session->status()==goldsrc::RuntimeReplaySessionStatus::active);
        if(selected) {
            CHECK(world.runtime_observation()->weapon_hud.health==0U);
            REQUIRE(world.runtime_observation()->life_events.size()==1U);
            CHECK(world.runtime_observation()->life_events[0].victim_entity==1U);
        }
        REQUIRE_FALSE(session.session->apply_record(record(death,103,13004,3)));
    }
}

TEST_CASE("E17 repeated four and eight bloodsprite hits retain Health DeathMsg ScoreInfo and active replay",
          "[e17][shotgun][e13-hit][e14][goldsrc][runtime-replay][transaction][stress]") {
    for(const std::size_t pellets:{4U,8U}) {
        CAPTURE(pellets);auto init=initialization();init.max_clients=2U;
        init.baselines=make_baselines(*init.schemas,init.generation,false,2U);
        init.receiving_player_entity=1U;
        init.user_message_definitions={{86U,1,"Health"},{88U,-1,"DeathMsg"},{201U,9,"ScoreInfo"}};
        const auto host=init.game_client;
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        REQUIRE(session.session->apply_record(record(initial_mixed(),100U,68001U,1U)));
        // Independent pinned public TE_BLOODSPRITE literal reused from the
        // existing E13 fixture. This is bounded synthetic player-hit pressure,
        // not a captured stock packet or local damage/hitgroup authority.
        const std::vector blood{std::byte{23},std::byte{115},std::byte{8},std::byte{0},
            std::byte{240},std::byte{255},std::byte{24},std::byte{0},std::byte{129},std::byte{0},
            std::byte{130},std::byte{0},std::byte{247},std::byte{3}};
        std::size_t kills=0U;
        for(std::uint32_t shot=1U;shot<=12U;++shot) {
            CAPTURE(shot);std::vector<std::byte> wire;
            for(std::size_t pellet=0;pellet<pellets;++pellet) {
                const auto at=wire.size();wire.insert(wire.end(),blood.begin(),blood.end());
                wire[at+2U]=static_cast<std::byte>(8U+pellet); // Independent finite contact coordinates.
            }
            const auto health=static_cast<std::uint8_t>(90U-shot);
            wire.insert(wire.end(),{std::byte{86},static_cast<std::byte>(health)});
            const bool death=shot%3U==0U;
            if(death) {
                ++kills;
                wire.insert(wire.end(),{std::byte{88},std::byte{10},std::byte{1},std::byte{2},
                    std::byte{'s'},std::byte{'h'},std::byte{'o'},std::byte{'t'},std::byte{'g'},
                    std::byte{'u'},std::byte{'n'},std::byte{0}});
                // Two exact registered ScoreInfo bodies, not local increments.
                wire.insert(wire.end(),{std::byte{201},std::byte{1},static_cast<std::byte>(kills),std::byte{0},
                    std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},
                    std::byte{201},std::byte{2},std::byte{0},std::byte{0},static_cast<std::byte>(kills),
                    std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}});
            }
            const auto before=world.runtime_observation();const auto revision=world.runtime_publication_revision();
            if(shot==6U) {
                auto malformed=wire;malformed.push_back(std::byte{255});
                REQUIRE_FALSE(session.session->apply_record(record(malformed,100U+shot,68001U+shot,1U+shot)));
                CHECK(world.runtime_observation()==before);CHECK(world.runtime_publication_revision()==revision);
                CHECK(session.session->status()==goldsrc::RuntimeReplaySessionStatus::active);
            }
            auto incoming=record(wire,100U+shot,68001U+shot,1U+shot);
            const auto accepted=session.session->apply_record(incoming);REQUIRE(accepted);REQUIRE(accepted.event);
            REQUIRE(accepted.event->decoded_batch.events.size()==pellets+(death ? 4U : 1U));
            for(std::size_t pellet=0;pellet<pellets;++pellet) {
                const auto* control=std::get_if<goldsrc::RuntimeControlEvent>(&accepted.event->decoded_batch.events[pellet]);
                REQUIRE(control);REQUIRE(std::holds_alternative<goldsrc::RuntimeControlBloodSprite>(control->body));
                const auto& body=std::get<goldsrc::RuntimeControlBloodSprite>(control->body);
                CHECK(body.coordinate_eighths[0]==8+static_cast<std::int16_t>(pellet));
                CHECK(body.sprite_references==std::array<std::int16_t,2>{129,130});
                CHECK(body.palette_color==247U);CHECK(body.scale==3U);
            }
            const auto committed=world.runtime_observation();REQUIRE(committed);
            CHECK(committed->weapon_hud.health==health);
            CHECK(committed->receiving_client==before->receiving_client); // No local pellet damage.
            CHECK(world.runtime_publication_revision()==revision+1U);
            CHECK(session.session->status()==goldsrc::RuntimeReplaySessionStatus::active);
            CHECK(committed->life_events.size()==(death ? 1U : 0U));
            if(death) {CHECK(committed->life_events[0].victim_entity==2U);CHECK(committed->life_events[0].weapon_text=="shotgun");}
            auto display=host->stage_record({committed.get(),*committed,{},1U,2U});REQUIRE(display);
            CHECK(display.state->multiplayer.players[0].frags==static_cast<std::int16_t>(kills));
            CHECK(display.state->multiplayer.players[1].deaths==static_cast<std::int16_t>(kills));
            REQUIRE_FALSE(session.session->apply_record(incoming));CHECK(world.runtime_observation()==committed);
            CHECK(world.runtime_publication_revision()==revision+1U);
            std::fill(incoming.payload.bytes.begin(),incoming.payload.bytes.end(),std::byte{0});
            const auto& retained=std::get<goldsrc::RuntimeControlBloodSprite>(
                std::get<goldsrc::RuntimeControlEvent>(accepted.event->decoded_batch.events[0]).body);
            CHECK(retained.coordinate_eighths[0]==8);CHECK(retained.sprite_references[0]==129);
        }
        CHECK(kills==4U);CHECK(session.session->status()==goldsrc::RuntimeReplaySessionStatus::active);
    }
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
    init.max_clients = 2U; // The other-victim notice is valid only in a two-client session.
    init.baselines=make_baselines(*init.schemas,init.generation,false,2U);
    init.receiving_player_entity = 1U;
    init.user_message_definitions = {{88,-1,"DeathMsg"}, {90,1,"ResetHUD"}, {89,0,"InitHUD"}};
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
    REQUIRE(session);
    const auto initial=session.session->apply_record(record(initial_mixed(),100,3000,1));
    INFO((initial.error ? initial.error->context : ""));REQUIRE(initial);
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
    CHECK(hlclient::games::halflife::cycle_owned_weapon(state, std::uint8_t{2}, 1) == 5U);
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

TEST_CASE("Runtime replay long sessions retain identity rollback and bounded reset semantics",
    "[goldsrc][runtime-replay][long-session][fingerprint-retention]") {
    constexpr std::size_t count=8'192U;
    auto init=initialization();
    init.limits.maximum_record_fingerprints=count+2U;
    client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(std::move(init),world);
    REQUIRE(session);
    const auto initial=record(initial_mixed(),100U,60'000U,1U);
    REQUIRE(session.session->apply_record(initial));
    const auto canonical=world.runtime_observation()->canonical_state_hash;
    const auto snapshot=session.session->decoder_state().current_snapshot();
    const auto client_frame=session.session->decoder_state().client_data_state().current_frame();
    std::vector<goldsrc::RuntimeReplayRecord> records;
    records.reserve(count);
    for(std::size_t i=0;i<count;++i) records.push_back(record(
        {std::byte{1}},static_cast<std::uint32_t>(101U+i),60'001U+i,2U+i));
    // Timing is descriptive only. Prebuilt owning wire records exercise the
    // production transaction; no wall-clock threshold makes the test flaky.
    for(std::size_t block=0;block<8U;++block) {
        const auto started=std::chrono::steady_clock::now();
        for(std::size_t i=block*1'024U;i<(block+1U)*1'024U;++i) {
            const auto applied=session.session->apply_record(records[i]);
            REQUIRE(applied);
        }
        const auto milliseconds=std::chrono::duration<double,std::milli>{
            std::chrono::steady_clock::now()-started}.count();
        std::cout<<"[runtime-fingerprint-cost] first="<<block*1'024U+1U
            <<" count=1024 milliseconds="<<milliseconds<<'\n';
        CHECK(world.runtime_observation()->canonical_state_hash==canonical);
        CHECK(session.session->decoder_state().current_snapshot()==snapshot);
        CHECK(session.session->decoder_state().client_data_state().current_frame()==client_frame);
    }
    CHECK(world.runtime_publication_revision()==count+2U);
    for(const auto* retained:std::array<const goldsrc::RuntimeReplayRecord*,3>{
            &initial,&records[count/2U],&records.back()}) {
        const auto before=world.runtime_observation();
        const auto duplicate=session.session->apply_record(*retained);
        REQUIRE_FALSE(duplicate);REQUIRE(duplicate.error);
        CHECK(duplicate.error->code==goldsrc::RuntimeReplayErrorCode::duplicate_record);
        auto changed=*retained;changed.payload.bytes.push_back(std::byte{1});
        const auto conflict=session.session->apply_record(changed);
        REQUIRE_FALSE(conflict);REQUIRE(conflict.error);
        CHECK(conflict.error->code==goldsrc::RuntimeReplayErrorCode::conflicting_record);
        CHECK(world.runtime_observation()==before);
    }
    const auto before=world.runtime_observation();
    auto retry=record({std::byte{255}},9'000U,90'000U,count+2U);
    const auto rejected=session.session->apply_record(retry);
    REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
    CHECK(rejected.error->code==goldsrc::RuntimeReplayErrorCode::decoder_failed);
    CHECK(world.runtime_observation()==before);
    retry.payload.bytes[0]=std::byte{1};
    REQUIRE(session.session->apply_record(retry));
    const auto full=world.runtime_observation();
    const auto exhausted=session.session->apply_record(record(
        {std::byte{1}},9'001U,90'001U,count+3U));
    REQUIRE_FALSE(exhausted);REQUIRE(exhausted.error);
    CHECK(exhausted.error->code==goldsrc::RuntimeReplayErrorCode::record_identity_limit_exceeded);
    CHECK(world.runtime_observation()==full);
    std::uint64_t generation=2U;
    for(const std::size_t limit:{1U,9U,2U}) {
        auto next=initialization(generation);next.limits.maximum_record_fingerprints=limit;
        REQUIRE_FALSE(session.session->reset_generation(std::move(next)));
        for(std::size_t i=0;i<limit;++i) REQUIRE(session.session->apply_record(record(
            {std::byte{1}},static_cast<std::uint32_t>(100U+i),60'000U+i,1U+i,generation)));
        const auto committed=world.runtime_observation();
        const auto limited=session.session->apply_record(record(
            {std::byte{1}},200U,70'000U,limit+1U,generation));
        REQUIRE_FALSE(limited);REQUIRE(limited.error);
        CHECK(limited.error->code==goldsrc::RuntimeReplayErrorCode::record_identity_limit_exceeded);
        CHECK(world.runtime_observation()==committed);
        ++generation;
    }
}

TEST_CASE("E13 advertised render effect and amount are neutral bounded atomic projections",
    "[e13][goldsrc][runtime-replay]") {
    for(const bool player_schema:{false,true}) for(const auto amount:{0U,2U,255U}) {
        auto init=initialization(1U,4096U,false,true);
        goldsrc::DeltaSchemaRegistryBuilder builder;
        for(const auto name:{"entity_state_t","entity_state_player_t","clientdata_t","weapon_data_t"}) {
            if(std::string_view{name}!=(player_schema ? "entity_state_player_t" : "entity_state_t")) {
                REQUIRE(builder.insert(*init.schemas->find_exact(name)));continue;
            }
            std::vector<fixture::Field> fields{std::begin(kEntityFields),std::end(kEntityFields)};
            // Deliberately unrelated SDK offsets: exact advertised names/types only.
            fields.push_back({"renderfx",8U,700U,8U});fields.push_back({"renderamt",8U,724U,8U});
            const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(fixture::schema(name,fields),0);
            REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
        }
        init.schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(builder).publish());
        init.baselines=make_baselines(*init.schemas,1U,true);
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        fixture::BitWriter writer;time_prefix(writer,100.0F);
        entity_header(writer,goldsrc::kGoldSrcSvcPacketEntitiesOpcode,1);
        writer.write(0,1);writer.write(0,1);writer.write(player_schema ? 1U : 2U,6);
        writer.write(0,1);writer.write(0,1);
        const std::pair<std::uint32_t,std::size_t> values[]{{17,8},{amount,8}};
        delta(writer,0xc0U,values);finish_entities(writer);
        const auto applied=session.session->apply_record(record(writer.bytes(),100,13001,1));
        INFO((applied.error ? applied.error->context : "published"));REQUIRE(applied);
        const auto published=world.runtime_observation();REQUIRE(published->packet_entities.size()==1);
        CHECK(published->packet_entities[0].render_effect==17);CHECK(published->packet_entities[0].render_amount==amount);
        auto changed=*published;changed.packet_entities[0].render_amount=42;
        CHECK(client::runtime_observation_canonical_hash(changed)==published->canonical_state_hash);
        CHECK(client::runtime_observation_visual_hash_v2(changed)==client::runtime_observation_visual_hash_v2(*published));
        CHECK(client::runtime_observation_visual_hash_v3(changed)!=client::runtime_observation_visual_hash_v3(*published));
        changed.packet_entities[0].render_amount=256;CHECK_FALSE(client::valid_runtime_observation(changed));
        auto bad=writer.bytes();bad.push_back(std::byte{255});
        REQUIRE_FALSE(session.session->apply_record(record(bad,101,13002,2)));
        CHECK(world.runtime_observation()==published);
        REQUIRE_FALSE(session.session->apply_record(record(writer.bytes(),100,13001,1)));
        CHECK(world.runtime_observation()==published);
        fixture::BitWriter retained;time_prefix(retained,100.1F);retained.write(1,8);
        REQUIRE(session.session->apply_record(record(retained.bytes(),102,13003,2)));
        CHECK(world.runtime_observation()->packet_entities[0].render_amount==amount);
        fixture::BitWriter update;time_prefix(update,100.2F);
        entity_header(update,goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode,1,std::uint8_t{100});
        update.write(0,1);update.write(0,1);update.write(player_schema ? 1U : 2U,6);update.write(0,1);
        const std::pair<std::uint32_t,std::size_t> zero[]{{0,8}};
        delta(update,0x80U,zero);finish_entities(update);
        REQUIRE(session.session->apply_record(record(update.bytes(),103,13004,3)));
        CHECK(world.runtime_observation()->packet_entities[0].render_effect==17);
        CHECK(world.runtime_observation()->packet_entities[0].render_amount==0);
        const auto after_delta=world.runtime_observation();
        auto truncated=writer.bytes();truncated.pop_back();
        REQUIRE_FALSE(session.session->apply_record(record(truncated,104,13005,4)));
        CHECK(world.runtime_observation()==after_delta);
        auto next=initialization(2U);REQUIRE_FALSE(session.session->reset_generation(next));
        CHECK(world.runtime_observation()->packet_entities.empty());
    }
}

TEST_CASE("E13 render effect and amount reject mismatched descriptor types without publication",
    "[e13][goldsrc][runtime-replay]") {
    for(const auto field:{"renderfx","renderamt"}) for(const auto type:{2U,1U,0x80000008U}) {
        auto init=initialization(1U,4096U,false,true);goldsrc::DeltaSchemaRegistryBuilder builder;
        for(const auto name:{"entity_state_player_t","clientdata_t","weapon_data_t"})
            REQUIRE(builder.insert(*init.schemas->find_exact(name)));
        std::vector<fixture::Field> fields{std::begin(kEntityFields),std::end(kEntityFields)};
        fields.push_back({field,type,701,8});
        const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(fixture::schema("entity_state_t",fields),0);
        REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
        init.schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(builder).publish());
        init.baselines=make_baselines(*init.schemas,1);
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        const auto before=world.runtime_observation();
        fixture::BitWriter writer;time_prefix(writer,100);
        entity_header(writer,goldsrc::kGoldSrcSvcPacketEntitiesOpcode,1);
        writer.write(0,1);writer.write(0,1);writer.write(2,6);writer.write(0,1);writer.write(0,1);
        const std::pair<std::uint32_t,std::size_t> value[]{{17,8}};
        delta(writer,0x40,value);finish_entities(writer);
        const auto rejected=session.session->apply_record(record(writer.bytes(),100,13021,1));
        REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
        CHECK(rejected.error->code==goldsrc::RuntimeReplayErrorCode::semantic_schema_mismatch);
        CHECK(world.runtime_observation()==before);
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
        CHECK_FALSE(entity.render_effect);CHECK_FALSE(entity.render_amount);
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

TEST_CASE("Server physics info remains bounded exact text with atomic projection",
    "[longjump][goldsrc][runtime-replay][prediction-seed]")
{
    for (const auto type : {0x80U, 0x08U}) {
        auto init = initialization();
        init.schemas = make_schemas(false, {}, false, false, false, false, {}, true, {}, {}, type);
        init.baselines = make_baselines(*init.schemas, 1U);
        client::ClientWorldState world;
        auto session = goldsrc::RuntimeReplaySession::initialize(std::move(init), world);
        REQUIRE(session);
        const auto before = world.runtime_observation();
        fixture::BitWriter writer;
        time_prefix(writer, 1.0F);
        writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
        writer.write(0U, 1U); writer.write(2U, 3U); writer.write(0x0100U, 16U);
        if (type == 0x80U) writer.string("\\other\\value\\slj\\1");
        else writer.write(1U, 8U);
        writer.write(0U, 1U); writer.align_zero();
        const auto applied = session.session->apply_record(record(writer.bytes(), 111U, 5300U, 1U));
        if (type == 0x80U) {
            REQUIRE(applied); REQUIRE(world.runtime_observation()->receiving_client);
            CHECK(world.runtime_observation()->receiving_client->physics_info == "\\other\\value\\slj\\1");
            const auto committed = world.runtime_observation();
            auto malformed = writer.bytes(); malformed.push_back(std::byte{255});
            CHECK_FALSE(session.session->apply_record(record(malformed, 112U, 5301U, 2U)));
            CHECK(world.runtime_observation() == committed);
            fixture::BitWriter retained;
            time_prefix(retained, 1.5F);
            retained.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
            retained.write(1U, 1U); retained.write(111U, 8U);
            delta(retained, 0U); retained.write(0U, 1U); retained.align_zero();
            REQUIRE(session.session->apply_record(record(retained.bytes(), 113U, 5302U, 2U)));
            CHECK(world.runtime_observation()->receiving_client->physics_info == "\\other\\value\\slj\\1");
            fixture::BitWriter removed;
            time_prefix(removed, 2.0F);
            removed.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
            removed.write(1U, 1U); removed.write(113U, 8U);
            removed.write(2U, 3U); removed.write(0x0100U, 16U);
            removed.string("\\slj\\0"); removed.write(0U, 1U); removed.align_zero();
            REQUIRE(session.session->apply_record(record(removed.bytes(), 114U, 5303U, 3U)));
            CHECK(world.runtime_observation()->receiving_client->physics_info == "\\slj\\0");
            REQUIRE_FALSE(session.session->reset_generation(initialization(2U)));
            CHECK_FALSE(world.runtime_observation()->receiving_client);
        } else {
            REQUIRE_FALSE(applied); REQUIRE(applied.error);
            CHECK(applied.error->code == goldsrc::RuntimeReplayErrorCode::semantic_schema_mismatch);
            CHECK(world.runtime_observation() == before);
        }
    }
}

TEST_CASE("E18 clientdata FOV exact unsigned float is optional atomic and not a guessed ABI field",
    "[e18][goldsrc][runtime-replay][camera]") {
    for(const std::uint32_t type:{4U,0x80000004U,8U}) {
        auto init=initialization();
        init.schemas=make_schemas(false,std::nullopt,false,false,false,false,std::nullopt,true,type);
        init.baselines=make_baselines(*init.schemas,1);client::ClientWorldState world;
        auto session=goldsrc::RuntimeReplaySession::initialize(std::move(init),world);REQUIRE(session);
        fixture::BitWriter writer;time_prefix(writer,1.0F);writer.write(goldsrc::kGoldSrcSvcClientDataOpcode,8);
        writer.write(0,1);writer.write(2,3);writer.write(1,8);writer.write(1,8);
        writer.write(goldsrc_signed(100),10);writer.write(40,8);writer.write(0,1);writer.align_zero();
        const auto result=session.session->apply_record(record(writer.bytes(),101,6001,1));
        if(type==4U) {
            INFO((result.error ? result.error->context : "ready"));REQUIRE(result);
            REQUIRE(world.runtime_observation()->receiving_client);
            CHECK(world.runtime_observation()->receiving_client->field_of_view==40.0);
            const auto before=world.runtime_observation();
            auto malformed=writer.bytes();malformed.push_back(std::byte{255});
            CHECK_FALSE(session.session->apply_record(record(malformed,102,6002,2)));
            CHECK(world.runtime_observation()==before);
        } else {
            CHECK_FALSE(result);REQUIRE(result.error);
            CHECK(result.error->code==goldsrc::RuntimeReplayErrorCode::semantic_schema_mismatch);
        }
    }
    auto init=initialization();client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(std::move(init),world);REQUIRE(session);
    REQUIRE(session.session->apply_record(record(receiving_client_origin_only(),103,6003,1)));
    CHECK_FALSE(world.runtime_observation()->receiving_client->field_of_view);
}

TEST_CASE("E20 exact negotiated auxiliary vector is optional signed float and atomic", "[e20][goldsrc][runtime-replay]") {
    for(const unsigned type:{0x80000004U,4U,8U}) {
        auto init=initialization();
        init.schemas=make_schemas(false,std::nullopt,false,false,false,false,std::nullopt,true,std::nullopt,type);
        init.baselines=make_baselines(*init.schemas,1);client::ClientWorldState world;
        auto session=goldsrc::RuntimeReplaySession::initialize(std::move(init),world);REQUIRE(session);
        fixture::BitWriter writer;time_prefix(writer,1.0F);writer.write(goldsrc::kGoldSrcSvcClientDataOpcode,8);
        writer.write(0,1);writer.write(2,3);writer.write(1,8);writer.write(3,8);
        writer.write(goldsrc_signed(100),10);writer.write(goldsrc_signed(1),10);writer.write(goldsrc_signed(2),10);
        writer.write(0,1);writer.align_zero();
        auto result=session.session->apply_record(record(writer.bytes(),101,6201,1));
        if(type==0x80000004U) {
            INFO((result.error ? result.error->context : "ready"));REQUIRE(result);
            const auto& v=world.runtime_observation()->receiving_client->auxiliary_vector2;
            CHECK_FALSE(v.x);CHECK(v.y==1.0);CHECK(v.z==2.0);
            auto before=world.runtime_observation();auto bad=writer.bytes();bad.push_back(std::byte{255});
            CHECK_FALSE(session.session->apply_record(record(bad,102,6202,2)));CHECK(world.runtime_observation()==before);
        } else {CHECK_FALSE(result);REQUIRE(result.error);CHECK(result.error->code==goldsrc::RuntimeReplayErrorCode::semantic_schema_mismatch);}
    }
    auto init=initialization();client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(std::move(init),world);REQUIRE(session);
    REQUIRE(session.session->apply_record(record(receiving_client_origin_only(),103,6203,1)));
    CHECK_FALSE(world.runtime_observation()->receiving_client->auxiliary_vector2.y);
    CHECK_FALSE(world.runtime_observation()->receiving_client->auxiliary_vector2.z);
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

TEST_CASE("Runtime replay retains latest svc_setangle with unchanged identity and canonical hash",
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
    REQUIRE(world.runtime_observation()->view_angle_correction);
    CHECK(world.runtime_observation()->view_angle_correction->source
              .source_transport_sequence == 60U);
    CHECK(world.runtime_observation()->view_angle_correction->yaw_degrees == 90.0);
    CHECK(world.runtime_observation()->canonical_state_hash == legacy_hash);
}


TEST_CASE("Spawn orientation survives later records before the first camera consumer",
          "[spawn-orientation][goldsrc][runtime-replay][view-angle]") {
    client::ClientWorldState world;
    auto initialized = goldsrc::RuntimeReplaySession::initialize(initialization(), world);
    REQUIRE(initialized);
    // Independent literal: pitch zero, yaw 180, roll zero. No local +180 fix.
    const std::vector<std::byte> correction{
        std::byte{10}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0x80}, std::byte{0}, std::byte{0}};
    REQUIRE(initialized.session->apply_record(record(correction, 60, 7001, 1)));
    const auto first = world.runtime_observation()->view_angle_correction;
    REQUIRE(first);
    for (unsigned index = 0; index < 4; ++index) {
        REQUIRE(initialized.session->apply_record(
            record({std::byte{1}}, 61 + index, 7002 + index, 2 + index)));
        REQUIRE(world.runtime_observation()->view_angle_correction);
        CHECK(world.runtime_observation()->view_angle_correction == first);
    }
    auto replacement = correction;
    replacement[4] = std::byte{0x40}; // yaw 90, new source even in same generation
    REQUIRE(initialized.session->apply_record(record(replacement, 65, 7006, 6)));
    REQUIRE(world.runtime_observation()->view_angle_correction);
    CHECK(world.runtime_observation()->view_angle_correction->yaw_degrees == 90.0);
    CHECK(world.runtime_observation()->view_angle_correction->source
              .source_transport_sequence == 65);
    REQUIRE(initialized.session->apply_record(record({std::byte{1}}, 66, 7007, 7)));
    CHECK(world.runtime_observation()->view_angle_correction->yaw_degrees == 90.0);
    const auto committed = world.runtime_observation();
    auto malformed = correction;
    malformed.push_back(std::byte{254});
    CHECK_FALSE(initialized.session->apply_record(record(malformed, 67, 7008, 8)));
    CHECK(world.runtime_observation() == committed);

    // A new session must never inherit the old session's correction.
    client::ClientWorldState next_world;
    auto next = goldsrc::RuntimeReplaySession::initialize(initialization(2), next_world);
    REQUIRE(next);
    CHECK_FALSE(next_world.runtime_observation()->view_angle_correction);
}

TEST_CASE("E13 unsupported temporary subtype survives owning failure and native wrapper summary",
          "[e13-hit-roundtrip][runtime-diagnostics][goldsrc][runtime-replay]") {
    auto init=initialization();init.user_message_definitions={{79,2,"AmmoPickup"},{83,2,"AmmoX"},{86,1,"Health"}};
    client::ClientWorldState world;
    const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
    REQUIRE(session.session->apply_record(record({std::byte{83},std::byte{1},std::byte{50}},90,901,1)));
    REQUIRE(session.session->apply_record(record({std::byte{86},std::byte{90},
        std::byte{79},std::byte{1},std::byte{17}},91,902,2)));
    const auto committed=world.runtime_observation();
    goldsrc::RuntimeReplayError retained;
    {
        const auto rejected=session.session->apply_record(record({std::byte{86},std::byte{33},
            std::byte{79},std::byte{1},std::byte{4},std::byte{23},std::byte{12},std::byte{1}},92,903,3));
        REQUIRE_FALSE(rejected);REQUIRE(rejected.error);retained=*rejected.error;
    }
    REQUIRE(retained.control_error);CHECK(retained.control_error->temporary_entity_type==12U);
    CHECK(world.runtime_observation()==committed);
    const auto summary=goldsrc::runtime_failure_summary(retained);
    CHECK(summary.find("temporary_entity_type=12")!=std::string::npos);
    CHECK(summary.find("message_start_byte=5")!=std::string::npos);
    CHECK(summary.find("failure_byte=7")!=std::string::npos);
    const auto hud=init.game_client->hud(*committed,1.1);
    CHECK(hud.inventory_notifications_received==1U);
    std::cout << "live_application_outcome result=error primary_error=runtime_record_failed"
        << " scripted_coverage=not_evaluated prediction_coverage=limited inventory_notifications="
        << hud.inventory_notifications_received
        << summary << '\n'; // Passed through existing fake-child/native/PowerShell harness.
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

TEST_CASE("E16 temporary effects are owning record-atomic observations with exact identities",
          "[e16][goldsrc][runtime-replay][temp-entity][transaction]") {
    const std::vector<std::byte> explosion{std::byte{23}, std::byte{3},
        std::byte{0xf7}, std::byte{0xff}, std::byte{0x12}, std::byte{0},
        std::byte{0x1b}, std::byte{0}, std::byte{0x34}, std::byte{0x12},
        std::byte{60}, std::byte{15}, std::byte{0}};
    auto smoke = explosion; smoke[1] = std::byte{5}; smoke.pop_back();
    client::ClientWorldState world;
    auto init = initialization();
    // Wire ownership/publication is independent of any concrete game module.
    init.game_client.reset();
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
    REQUIRE(session);
    const auto initial = world.runtime_observation();
    auto truncated = explosion; truncated.pop_back();
    std::vector<std::byte> bad{std::byte{1}};
    bad.insert(bad.end(), explosion.begin(), explosion.end());
    bad.insert(bad.end(), truncated.begin(), truncated.end());
    const auto failure = session.session->apply_record(record(std::move(bad), 100, 61001, 1));
    REQUIRE_FALSE(failure); REQUIRE(failure.error->control_error);
    CHECK(failure.error->control_error->temporary_entity_type == 3U);
    CHECK(world.runtime_observation() == initial);
    CHECK(session.session->publication_revision() == 1U);
    auto stream = explosion;
    stream.insert(stream.end(), explosion.begin(), explosion.end()); // Same place, distinct message identity.
    auto incoming = record(std::move(stream), 100, 61001, 1);
    incoming.received_at = goldsrc::SoundTime{} + std::chrono::seconds{20};
    auto committed = session.session->apply_record(incoming);
    REQUIRE(committed); REQUIRE(committed.event->temporary_effects.size() == 2U);
    const auto retained = committed.event->temporary_effects;
    CHECK(retained[0].kind == client::TemporaryEffectKind::explosion);
    CHECK(retained[0].generation == 1U); CHECK(retained[0].record == 61001U);
    CHECK(retained[0].record_ordinal == 1U); CHECK(retained[0].message_bit_offset == 0U);
    CHECK(retained[1].message_bit_offset == 104U); CHECK(retained[0].entry_ordinal == 0U);
    CHECK(retained[0].received_at_seconds == 20.0);
    CHECK(retained[0].origin.x == -1.125F); CHECK(retained[0].origin.y == 2.25F);
    CHECK(retained[0].origin.z == 3.375F); CHECK(retained[0].sprite_index == 0x1234U);
    CHECK(retained[0].scale_tenths == 60U); CHECK(retained[0].framerate == 15U);
    auto duplicate = session.session->apply_record(incoming);
    REQUIRE_FALSE(duplicate); CHECK(duplicate.error->code == goldsrc::RuntimeReplayErrorCode::duplicate_record);
    CHECK(session.session->publication_revision() == 2U);
    auto independent = session.session->apply_record(record(explosion, 101, 61002, 2));
    REQUIRE(independent); REQUIRE(independent.event->temporary_effects.size() == 1U);
    CHECK(independent.event->temporary_effects[0].record == 61002U);
    auto no_effect = session.session->apply_record(record({std::byte{1}}, 102, 61003, 3));
    REQUIRE(no_effect); CHECK(no_effect.event->temporary_effects.empty()); // No retained replay at render/update.
    auto server_smoke = session.session->apply_record(record(smoke, 103, 61004, 4));
    REQUIRE(server_smoke); REQUIRE(server_smoke.event->temporary_effects.size() == 1U);
    CHECK(server_smoke.event->temporary_effects[0].kind == client::TemporaryEffectKind::smoke);
    CHECK(server_smoke.event->temporary_effects[0].flags == 0U);
    auto next = initialization(2U); next.game_client.reset();
    REQUIRE_FALSE(session.session->reset_generation(next));
    auto stale = session.session->apply_record(incoming);
    REQUIRE_FALSE(stale); CHECK(stale.error->code == goldsrc::RuntimeReplayErrorCode::generation_mismatch);
    incoming.payload.bytes.clear(); session.session.reset();
    CHECK(retained[0].origin.x == -1.125F); CHECK(retained[0].sprite_index == 0x1234U);
}

TEST_CASE("E16 commit delivers temp effects through normal game host without partial visuals or duplicate debris",
          "[e16][goldsrc][runtime-replay][game-module][temp-entity][transaction]") {
    const std::vector<std::byte> explosion{std::byte{23}, std::byte{3},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{7}, std::byte{0}, std::byte{60}, std::byte{15}, std::byte{0}};
    auto init = initialization();
    auto host = init.game_client; host->set_movement_audio_time_origin(100.0);
    const auto sounds = std::make_shared<goldsrc::CommittedSoundQueue>(); init.sound_events = sounds;
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world); REQUIRE(session);
    auto bytes = explosion; bytes.insert(bytes.end(), explosion.begin(), explosion.end()); bytes.pop_back();
    auto bad = record(bytes, 200, 62001, 1); bad.received_at = goldsrc::SoundTime{} + std::chrono::seconds{101};
    CHECK_FALSE(session.session->apply_record(bad));
    auto batch = host->drain_temporary_effects(1.0); CHECK(batch.count == 0U); CHECK(batch.statistics.received == 0U);
    bytes = explosion; bytes.insert(bytes.end(), event_fixture::kSound.begin(), event_fixture::kSound.end());
    auto incoming = record(bytes, 200, 62001, 1); incoming.received_at = *bad.received_at;
    REQUIRE(session.session->apply_record(incoming));
    batch = host->drain_temporary_effects(1.0); REQUIRE(batch.count == 1U);
    REQUIRE(batch.effects[0].sprite); CHECK(batch.effects[0].sprite->model_slot == 7U);
    CHECK(batch.effects[0].starts_at_seconds == 1.0); REQUIRE(batch.effects[0].light); REQUIRE(batch.effects[0].particles);
    CHECK(sounds->starts == 1U); // Existing svc_sound only, not a synthetic second debris cue.
    CHECK_FALSE(session.session->apply_record(incoming));
    CHECK(host->drain_temporary_effects(1.0).count == 0U); CHECK(sounds->starts == 1U);
    auto independent = record(explosion, 201, 62002, 2); independent.received_at = *bad.received_at;
    REQUIRE(session.session->apply_record(independent));
    CHECK(host->drain_temporary_effects(1.0).count == 1U); // Same coordinates, legitimate distinct commit.
    REQUIRE(session.session->apply_record(record({std::byte{1}}, 202, 62003, 3)));
    CHECK(host->drain_temporary_effects(1.3).count == 0U); // No local smoke timer.
    auto smoke = explosion; smoke[1] = std::byte{5}; smoke.pop_back();
    auto server_smoke = record(smoke, 203, 62004, 4);
    server_smoke.received_at = goldsrc::SoundTime{} + std::chrono::milliseconds{101300};
    REQUIRE(session.session->apply_record(server_smoke));
    batch = host->drain_temporary_effects(1.3); REQUIRE(batch.count == 1U);
    REQUIRE(batch.effects[0].sprite); CHECK(batch.effects[0].sprite->blend == hlclient::game_api::TemporarySpriteBlend::alpha_blended);
    CHECK_FALSE(batch.effects[0].light); CHECK_FALSE(batch.effects[0].particles);
}

TEST_CASE("Snark sound visual crosses atomic runtime commit without requiring an audio queue or extra audio",
    "[e27][sound-pop][runtime-replay][transaction][no-stock]") {
    namespace pop=hlclient::tests::snark_sound_pop_fixture;
    auto init=initialization();auto host=init.game_client;
    host->set_movement_audio_time_origin(100);
    client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
    pop::bind(*host,5); // Fixture's actual decoded slot, not production's observed 54.
    pop::entities(*host,1,19);
    auto bytes=event_fixture::bytes(event_fixture::kSound);
    auto incoming=record(bytes,100,72001,2);
    incoming.received_at=goldsrc::SoundTime{}+std::chrono::seconds{101};
    auto invalid=incoming;invalid.payload.bytes.push_back(std::byte{255});
    const auto before=world.runtime_observation();CHECK_FALSE(session.session->apply_record(invalid));
    CHECK(world.runtime_observation()==before);CHECK(host->drain_temporary_effects(1).count==0);
    REQUIRE(session.session->apply_record(incoming));
    const auto batch=host->drain_temporary_effects(1);REQUIRE(batch.count==1);
    CHECK(batch.statistics.pop_sound_accepted==1);CHECK(batch.statistics.blood_received==0);
    CHECK(batch.effects[0].starts_at_seconds==1);CHECK(batch.effects[0].identity.record==72001);
    CHECK(batch.effects[0].origin.x==0);CHECK(batch.effects[0].origin.y==0);CHECK(batch.effects[0].origin.z==0);
    REQUIRE(batch.effects[0].sprite);REQUIRE(batch.effects[0].sprite_burst);
    CHECK(batch.effects[0].sprite->model_slot==321);CHECK(batch.effects[0].sprite_burst->sprite.model_slot==322);
    CHECK(batch.effects[0].sprite_burst->count==16);CHECK_FALSE(batch.effects[0].secondary_sprite);
    CHECK_FALSE(batch.effects[0].sound);CHECK_FALSE(init.sound_events);
    CHECK_FALSE(session.session->apply_record(incoming));CHECK(host->drain_temporary_effects(1).count==0);
    CHECK(host->drain_audio().count==0);CHECK(host->drain_remote_audio().count==0);
}

TEST_CASE("E16 observed beam follow commits without synthetic effects and rejects malformed suffix",
          "[e16][e20][manual-fix][goldsrc][runtime-replay][temp-entity][transaction]") {
    const std::vector<std::byte> beam{std::byte{23},std::byte{22},
        std::byte{7},std::byte{0},std::byte{9},std::byte{0},
        std::byte{10},std::byte{3},std::byte{255},std::byte{255},std::byte{255},std::byte{255}};
    auto init = initialization(); auto host = init.game_client;
    host->set_movement_audio_time_origin(100.0);
    const auto sounds = std::make_shared<goldsrc::CommittedSoundQueue>(); init.sound_events = sounds;
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world); REQUIRE(session);
    const auto before = world.runtime_observation();
    auto bytes = beam; bytes.insert(bytes.end(), beam.begin(), beam.end()); bytes.pop_back();
    auto bad = record(bytes, 300, 63001, 1);
    bad.received_at = goldsrc::SoundTime{} + std::chrono::seconds{101};
    CHECK_FALSE(session.session->apply_record(bad));
    CHECK(world.runtime_observation() == before);
    CHECK(host->drain_temporary_effects(1.0).count == 0U); CHECK(sounds->starts == 0U);
    CHECK(host->drain_follow_events().count==0U);
    bytes = beam; bytes.insert(bytes.end(), event_fixture::kSound.begin(), event_fixture::kSound.end());
    auto incoming = record(bytes, 300, 63001, 1); incoming.received_at = *bad.received_at;
    const auto committed = session.session->apply_record(incoming); REQUIRE(committed);
    // E20 implements this previously inert generic server effect, not a fake
    // grenade/HL callback. Keep strict payload, atomicity and duplicate checks.
    REQUIRE(committed.event->temporary_effects.size()==1U);
    CHECK(committed.event->temporary_effects[0].kind==client::TemporaryEffectKind::beam_follow);
    const auto follows=host->drain_follow_events();REQUIRE(follows.count==1U);
    CHECK(follows.events[0].entity_attachment==7U);CHECK(follows.events[0].sprite_index==9U);
    CHECK(follows.events[0].life_tenths==10U);CHECK(follows.events[0].width_tenths==3U);
    CHECK(host->drain_temporary_effects(1.0).count == 0U); CHECK(sounds->starts == 1U);
    CHECK_FALSE(session.session->apply_record(incoming)); CHECK(sounds->starts == 1U);
    CHECK(host->drain_follow_events().count==0U);
}

TEST_CASE("E17 negotiated numeric special reload and pump fields retain owned observations",
          "[e17][goldsrc][runtime-replay][weapon-data][special-reload]")
{
    auto init=initialization();
    init.schemas=make_schemas(false,std::nullopt,false,false,false,true);
    init.baselines=make_baselines(*init.schemas,init.generation);
    client::ClientWorldState world;
    const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);
    REQUIRE(session);
    fixture::BitWriter first;
    time_prefix(first,100.0F);
    const std::pair<std::uint32_t,std::size_t> start[]{{goldsrc_signed(2U),10U},
        {1U,2U},{goldsrc_signed(500U),22U}};
    special_weapon_fields(first,0x31U,start);
    auto incoming=record(first.bytes(),100U,67001U,1U);
    REQUIRE(session.session->apply_record(incoming));
    const auto started=world.runtime_observation();
    REQUIRE(started);REQUIRE(started->weapon_slots.size()>2U);
    CHECK(started->weapon_slots[2U].clip==2);
    CHECK(started->weapon_slots[2U].special_reload_state==1);
    REQUIRE(started->weapon_slots[2U].pump_time);
    CHECK(*started->weapon_slots[2U].pump_time==Catch::Approx(0.5));
    CHECK(started->client_metadata.freshness==client::RuntimeObservationFreshness::observed_in_record);
    std::fill(incoming.payload.bytes.begin(),incoming.payload.bytes.end(),std::byte{255});
    CHECK(started->weapon_slots[2U].special_reload_state==1); // No RX-buffer retention.

    fixture::BitWriter retained;time_prefix(retained,100.1F);retained.write(1U,8U);
    REQUIRE(session.session->apply_record(record(retained.bytes(),101U,67002U,2U)));
    CHECK(world.runtime_observation()->client_metadata.freshness==client::RuntimeObservationFreshness::retained);
    CHECK(world.runtime_observation()->weapon_slots[2U].special_reload_state==1);
    CHECK(world.runtime_observation()->weapon_slots[2U].pump_time==started->weapon_slots[2U].pump_time);

    // Fresh delta inherits pump/clip but changes the numeric state to 2.
    fixture::BitWriter inserting;time_prefix(inserting,100.2F);
    const std::pair<std::uint32_t,std::size_t> insert[]{{2U,2U}};
    special_weapon_fields(inserting,0x10U,insert,std::uint8_t{100});
    REQUIRE(session.session->apply_record(record(inserting.bytes(),102U,67003U,3U)));
    const auto inserted=world.runtime_observation();
    CHECK(inserted->client_metadata.freshness==client::RuntimeObservationFreshness::observed_in_record);
    CHECK(inserted->weapon_slots[2U].special_reload_state==2);
    CHECK(inserted->weapon_slots[2U].clip==2);
    CHECK(inserted->weapon_slots[2U].pump_time==started->weapon_slots[2U].pump_time);

    fixture::BitWriter transfer;time_prefix(transfer,100.3F);
    const std::pair<std::uint32_t,std::size_t> transferred[]{{goldsrc_signed(3U),10U},{1U,2U}};
    special_weapon_fields(transfer,0x11U,transferred,std::uint8_t{102});
    REQUIRE(session.session->apply_record(record(transfer.bytes(),103U,67004U,4U)));
    CHECK(world.runtime_observation()->weapon_slots[2U].clip==3);
    CHECK(world.runtime_observation()->weapon_slots[2U].special_reload_state==1);
    CHECK(started->weapon_slots[2U].clip==2);CHECK(inserted->weapon_slots[2U].special_reload_state==2);

    fixture::BitWriter finish;time_prefix(finish,100.4F);
    const std::pair<std::uint32_t,std::size_t> finished[]{{0U,2U}};
    special_weapon_fields(finish,0x10U,finished,std::uint8_t{103});
    REQUIRE(session.session->apply_record(record(finish.bytes(),104U,67005U,5U)));
    CHECK(world.runtime_observation()->weapon_slots[2U].special_reload_state==0);
    CHECK(world.runtime_observation()->weapon_slots[2U].clip==3);

    // Value 3 is a neutral advertised integer, not protocol-side Shotgun policy.
    fixture::BitWriter numeric;time_prefix(numeric,100.5F);
    const std::pair<std::uint32_t,std::size_t> value_three[]{{3U,2U}};
    special_weapon_fields(numeric,0x10U,value_three,std::uint8_t{104});
    REQUIRE(session.session->apply_record(record(numeric.bytes(),105U,67006U,6U)));
    CHECK(world.runtime_observation()->weapon_slots[2U].special_reload_state==3);
    auto changed=*world.runtime_observation();
    changed.weapon_slots[2U].special_reload_state=0;
    changed.weapon_slots[2U].pump_time=0.75;
    CHECK(client::runtime_observation_canonical_hash(changed)==world.runtime_observation()->canonical_state_hash);
    changed.weapon_slots[2U].pump_time=std::numeric_limits<double>::infinity();
    CHECK_FALSE(client::valid_runtime_observation(changed));
}

TEST_CASE("E17 absent exact weapon descriptors stay unavailable without fallback timers",
          "[e17][goldsrc][runtime-replay][weapon-data][special-reload]")
{
    SECTION("Legacy schema does not acquire new optional values") {
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(initialization(),world);
        REQUIRE(session);REQUIRE(session.session->apply_record(record(initial_mixed(),100U,67101U,1U)));
        for (const auto& weapon:world.runtime_observation()->weapon_slots) {
            CHECK_FALSE(weapon.special_reload_state);CHECK_FALSE(weapon.pump_time);
        }
    }
    SECTION("Numeric special state does not fabricate an absent pump descriptor") {
        auto init=initialization();
        init.schemas=make_schemas(false,std::nullopt,false,false,false,true,std::nullopt,false);
        init.baselines=make_baselines(*init.schemas,init.generation);
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        fixture::BitWriter writer;
        time_prefix(writer,100.0F);
        const std::pair<std::uint32_t,std::size_t> values[]{{2U,2U}};
        special_weapon_fields(writer,0x10U,values);
        REQUIRE(session.session->apply_record(record(writer.bytes(),100U,67102U,1U)));
        CHECK(world.runtime_observation()->weapon_slots[2U].special_reload_state==2);
        CHECK_FALSE(world.runtime_observation()->weapon_slots[2U].pump_time);
    }
}

TEST_CASE("E17 malformed weapon field type or suffix publishes neither game prefix nor weapon state",
          "[e17][goldsrc][runtime-replay][weapon-data][special-reload][transaction]")
{
    for (const auto type:{0x0000'0001U,0x0000'0002U,0x8000'0008U,0x8000'0004U}) {
        CAPTURE(type);
        auto init=initialization();
        init.schemas=make_schemas(false,std::nullopt,false,false,false,true,type);
        init.baselines=make_baselines(*init.schemas,init.generation);
        init.user_message_definitions={{93U,1,"Health"}};
        const auto host=init.game_client;
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        REQUIRE(session.session->apply_record(record({std::byte{93},std::byte{77}},100U,67201U,1U)));
        const auto before=world.runtime_observation();
        const auto hud=host->hud(*before,1.0);
        const auto history=session.session->decoder_state().client_data_state().history().frame_count();
        fixture::BitWriter writer;time_prefix(writer,100.1F);writer.write(93U,8U);writer.write(31U,8U);
        const std::pair<std::uint32_t,std::size_t> values[]{{0U,2U}};
        special_weapon_fields(writer,0x10U,values);
        const auto rejected=session.session->apply_record(record(writer.bytes(),101U,67202U,2U));
        REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
        CHECK(rejected.error->code==goldsrc::RuntimeReplayErrorCode::semantic_schema_mismatch);
        CHECK(rejected.error->context.find("m_fInSpecialReload")!=std::string::npos);
        CHECK(world.runtime_observation()==before);
        CHECK(world.runtime_observation()->canonical_state_hash==before->canonical_state_hash);
        CHECK(world.runtime_observation()->weapon_hud.health==77U);
        CHECK(host->hud(*before,1.1).inventory_feedback_revision==hud.inventory_feedback_revision);
        CHECK(session.session->decoder_state().client_data_state().history().frame_count()==history);
        REQUIRE(session.session->apply_record(record({std::byte{1}},102U,67203U,3U)));
        CHECK(world.runtime_observation()->weapon_hud.health==77U); // Staged module state rolled back too.
    }
    SECTION("Valid negotiated prefix plus malformed service suffix is atomic and retryable") {
        auto init=initialization();
        init.schemas=make_schemas(false,std::nullopt,false,false,false,true);
        init.baselines=make_baselines(*init.schemas,init.generation);
        init.user_message_definitions={{93U,1,"Health"}};
        const auto host=init.game_client;
        client::ClientWorldState world;
        const auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        REQUIRE(session.session->apply_record(record({std::byte{93},std::byte{77}},100U,67301U,1U)));
        const auto before=world.runtime_observation();
        fixture::BitWriter writer;time_prefix(writer,100.1F);writer.write(93U,8U);writer.write(31U,8U);
        const std::pair<std::uint32_t,std::size_t> values[]{{2U,2U}};
        special_weapon_fields(writer,0x10U,values);
        auto malformed=writer.bytes();malformed.push_back(std::byte{255});
        REQUIRE_FALSE(session.session->apply_record(record(malformed,101U,67302U,2U)));
        CHECK(world.runtime_observation()==before);
        CHECK(world.runtime_observation()->canonical_state_hash==before->canonical_state_hash);
        CHECK(host->hud(*before,1.0).inventory_notifications_received==0U);
        auto good=record(writer.bytes(),101U,67302U,2U);
        REQUIRE(session.session->apply_record(good));
        const auto committed=world.runtime_observation();
        CHECK(committed->weapon_slots[2U].special_reload_state==2);
        CHECK(committed->weapon_hud.health==31U);
        REQUIRE_FALSE(session.session->apply_record(good));
        CHECK(world.runtime_observation()==committed);
    }
}

TEST_CASE("E19 exact signed event parameters survive atomic replay and feed assigned remote Crossbow semantics",
          "[e19][runtime-replay][scripted-events][transaction]") {
    namespace cb=hlclient::tests::crossbow_fixture;
    auto init=initialization();init.max_clients=8;init.receiving_player_entity=1;
    goldsrc::DeltaSchemaRegistryBuilder builder;for(const auto& schema:init.schemas->schemas())REQUIRE(builder.insert(schema));
    const fixture::Field fields[]{
        {"entindex",0x0000'0008U,0,8},{"origin[0]",0x8000'0004U,4,16,32000,4000},
        {"origin[1]",0x8000'0004U,8,16,32000,4000},{"origin[2]",0x8000'0004U,12,16,32000,4000},
        {"angles[0]",0x0000'0010U,16,16},{"angles[1]",0x0000'0010U,20,16},{"angles[2]",0x0000'0010U,24,16},
        {"iparam1",0x8000'0008U,28,16},{"iparam2",0x8000'0008U,32,16}};
    const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(fixture::schema("event_t",fields),0);REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
    init.schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(builder).publish());
    init.baselines=make_baselines(*init.schemas,1);auto host=init.game_client;
    host->set_movement_audio_time_origin(100);client::ClientWorldState world;
    auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
    const std::array bindings{cb::binding(),cb::binding(true,902)};host->configure_scripted_events(bindings);
    (void)host->drain_remote_audio();
    fixture::BitWriter writer;writer.write(21,8);writer.write(902,10);
    writer.write(2,3);writer.write(0xff,8);writer.write(1,8);
    writer.write(3,8);writer.write(goldsrc_signed(800),16);writer.write(goldsrc_signed(640),16);writer.write(goldsrc_signed(416),16);
    writer.write(0,16);writer.write(32768,16);writer.write(0,16);writer.write(goldsrc_signed(4),16);writer.write(goldsrc_signed(10),16);
    writer.write(0,1);writer.align_zero();
    auto incoming=record(writer.bytes(),300,65001,1);incoming.received_at=goldsrc::SoundTime{}+std::chrono::milliseconds{100030};
    auto malformed=incoming;malformed.payload.bytes.push_back(std::byte{255});const auto before=world.runtime_observation();
    CHECK_FALSE(session.session->apply_record(malformed));CHECK(world.runtime_observation()==before);
    CHECK(host->drain_remote_effects(.03).count==0);CHECK(host->drain_remote_audio().count==0);
    const auto committed=session.session->apply_record(incoming);REQUIRE(committed);
    incoming.payload.bytes.clear();
    const auto batch=host->drain_remote_effects(.031);REQUIRE(batch.count==1);REQUIRE(batch.effects[0].impact);
    CHECK(batch.effects[0].action.weapon_id==6);CHECK(batch.effects[0].impact->suppress_decal);
    CHECK(batch.effects[0].impact->origin.x==100);CHECK(batch.effects[0].impact->origin.z==80);
    const auto audio=host->drain_remote_audio();REQUIRE(audio.count==2);CHECK(audio.cues[0].reference.name()=="weapons/xbow_fire1.wav");
    CHECK(host->drain_remote_effects(.031).count==0);
}
TEST_CASE("E21 exact magnitude primary flag and delayed stop remain atomic across reliable records", "[e21][runtime-replay][transaction][no-stock]") {
    auto init=initialization();init.max_clients=8;init.receiving_player_entity=1;
    goldsrc::DeltaSchemaRegistryBuilder builder;for(const auto& schema:init.schemas->schemas())REQUIRE(builder.insert(schema));
    const fixture::Field fields[]{
        {"entindex",0x00000008U,0,8},{"origin[0]",0x80000004U,4,16,32000,4000},{"origin[1]",0x80000004U,8,16,32000,4000},{"origin[2]",0x80000004U,12,16,32000,4000},
        {"angles[0]",0x00000010U,16,16},{"angles[1]",0x00000010U,20,16},{"angles[2]",0x00000010U,24,16},
        {"fparam1",0x80000004U,28,16,32000,4000},{"bparam1",0x00000008U,32,1},{"bparam2",0x00000008U,36,1}};
    const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(fixture::schema("event_t",fields),0);REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
    init.schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(builder).publish());init.baselines=make_baselines(*init.schemas,1);
    auto host=init.game_client;host->set_movement_audio_time_origin(100);client::ClientWorldState world;auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
    hlclient::game_api::ScriptedEventBinding binding;binding.index=900;constexpr std::string_view name="events/gauss.sc";std::copy(name.begin(),name.end(),binding.name.begin());host->configure_scripted_events(std::span{&binding,1U});
    const auto encoded=[](bool stop) {
        fixture::BitWriter w;w.write(21,8);w.write(900,10);w.write(2,3);w.write(0xff,8);w.write(3,8);
        w.write(2,8);w.write(goldsrc_signed(800),16);w.write(goldsrc_signed(640),16);w.write(goldsrc_signed(416),16);
        w.write(0,16);w.write(32768,16);w.write(0,16);w.write(goldsrc_signed(stop ? 0 : 1600),16);w.write(0,1);w.write(stop ? 1 : 0,1);
        w.write(stop ? 1 : 0,1);if(stop)w.write(1,16);w.align_zero();return w.bytes();
    };
    auto incoming=record(encoded(false),300,67501,1);incoming.received_at=goldsrc::SoundTime{}+std::chrono::milliseconds{100030};
    auto malformed=incoming;malformed.payload.bytes.push_back(std::byte{255});const auto before=world.runtime_observation();
    CHECK_FALSE(session.session->apply_record(malformed));CHECK(world.runtime_observation()==before);CHECK(host->drain_audio().count==0);
    REQUIRE(session.session->apply_record(incoming));incoming.payload.bytes.clear();auto sound=host->drain_audio();REQUIRE(sound.count==1);CHECK(sound.cues[0].reference.name()=="weapons/gauss2.wav");CHECK(sound.cues[0].volume==1);
    auto stop=record(encoded(true),301,67502,2);stop.received_at=incoming.received_at;REQUIRE(session.session->apply_record(stop));CHECK(host->drain_audio().count==0);CHECK_FALSE(session.session->apply_record(stop));
}
TEST_CASE("E21 explicitly transmitted Gauss model pitch survives the real event publisher", "[e21][gauss-remote-aim][runtime-replay][scripted-events][transaction][no-stock]") {
    namespace api=hlclient::game_api;using V=hlclient::assets::AssetVector3;
    struct Query final:api::IStaticEffectTrace {
        mutable V eye{},target{};mutable unsigned calls{};
        api::StaticEffectTrace trace(V a,V b) const noexcept override {
            ++calls;eye=a;target=b;return {true,false,false,false,false,b,{}};
        }
    };
    for(const bool reliable:{false,true})for(const bool primary:{true,false})for(const bool ducking:{false,true}) {
        CAPTURE(reliable,primary,ducking);auto init=initialization();init.max_clients=8;init.receiving_player_entity=1;
        goldsrc::DeltaSchemaRegistryBuilder builder;
        for(const auto name:{"clientdata_t","weapon_data_t"})REQUIRE(builder.insert(*init.schemas->find_exact(name)));
        std::vector<fixture::Field> player_fields{std::begin(kEntityFields),std::end(kEntityFields)};
        player_fields.push_back({"weaponmodel",0x00000008U,24,16});
        const auto player=goldsrc::DeltaDescriptionParser{}.parse(fixture::schema("entity_state_t",player_fields),0);
        REQUIRE(player);REQUIRE(builder.insert(*player.schema));
        const fixture::Field fields[]{
            {"entindex",0x00000008U,0,8},{"origin[0]",0x80000004U,4,16,32000,4000},{"origin[1]",0x80000004U,8,16,32000,4000},{"origin[2]",0x80000004U,12,16,32000,4000},
            {"angles[0]",0x00000010U,16,16},{"angles[1]",0x00000010U,20,16},{"angles[2]",0x00000010U,24,16},
            {"fparam1",0x80000004U,28,16,32000,4000},{"bparam1",0x00000008U,32,1},{"bparam2",0x00000008U,36,1},{"ducking",0x00000008U,40,1}};
        const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(fixture::schema("event_t",fields),0);
        REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
        init.schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(builder).publish());
        init.baselines=make_baselines(*init.schemas,1,false,8);auto host=init.game_client;host->set_movement_audio_time_origin(100);
        client::ClientWorldState world;auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
        api::ScriptedEventBinding binding;binding.index=900;constexpr std::string_view name="events/gauss.sc";
        std::copy(name.begin(),name.end(),binding.name.begin());host->configure_scripted_events(std::span{&binding,1U});
        fixture::BitWriter w;time_prefix(w,100.03F);
        entity_header(w,goldsrc::kGoldSrcSvcPacketEntitiesOpcode,1);
        w.write(0,1);w.write(0,1);w.write(2,6);w.write(0,1);w.write(0,1);
        const std::pair<std::uint32_t,std::size_t> values[]{{goldsrc_signed(256),16},{71,16}};
        delta(w,0x41,values);finish_entities(w);
        w.write(reliable ? 21 : 3,8);if(!reliable)w.write(1,5);w.write(900,10);
        if(!reliable){w.write(1,1);w.write(0,11);w.write(1,1);} // packet0, explicit delta
        w.write(2,3);w.write(0xff,8);w.write(7,8);
        w.write(2,8);w.write(goldsrc_signed(256),16);w.write(goldsrc_signed(256),16);w.write(goldsrc_signed(128),16);
        w.write(60075,16);w.write(0,16);w.write(0,16); // independently quantized SDK model pitch330 degrees
        w.write(goldsrc_signed(primary ? 160 : 1600),16);w.write(primary ? 1 : 0,1);w.write(0,1);w.write(ducking ? 1 : 0,1);
        w.write(0,1);w.align_zero();
        auto incoming=record(w.bytes(),300,67801,1);incoming.received_at=goldsrc::SoundTime{}+std::chrono::milliseconds{100030};
        auto malformed=incoming;malformed.payload.bytes.push_back(std::byte{255});const auto before=world.runtime_observation();Query query;
        CHECK_FALSE(session.session->apply_record(malformed));CHECK(world.runtime_observation()==before);
        CHECK(host->beam_effects(query,.031,{}).shots==0);CHECK(query.calls==0);CHECK(host->drain_audio().count==0);
        const auto committed=session.session->apply_record(incoming);REQUIRE(committed);
        const goldsrc::RuntimeControlScriptedEvents* body=nullptr;
        for(const auto& item:committed.event->decoded_batch.events)
            if(const auto* control=std::get_if<goldsrc::RuntimeControlEvent>(&item))
                if(const auto* events=std::get_if<goldsrc::RuntimeControlScriptedEvents>(&control->body))body=events;
        REQUIRE(body);CHECK(body->reliable==reliable);REQUIRE(body->entries.size()==1);
        CHECK((body->entries[0].argument_field_mask&(std::uint64_t{1}<<4))!=0);
        const auto duplicate=incoming;incoming.payload.bytes.clear();const auto beam=host->beam_effects(query,.031,{});
        REQUIRE(beam.count==1);CHECK(query.calls==1);CHECK(beam.shots==1);
        CHECK(query.eye.z==(ducking ? 28.F : 44.F));CHECK((query.target.z-query.eye.z)/8192==Catch::Approx(-1).margin(1e-5));
        CHECK(beam.sprites[0].end.z<beam.sprites[0].start.z);CHECK(beam.sprites[0].width==Catch::Approx(primary ? 2.975 : 5.95));
        REQUIRE(host->drain_audio().count==1);CHECK_FALSE(session.session->apply_record(duplicate));
        CHECK(host->beam_effects(query,.032,{}).shots==1);CHECK(query.calls==1);CHECK(host->drain_audio().count==0);
        host->teardown();CHECK(host->beam_effects(query,.033,{}).count==0);
    }
}
TEST_CASE("E19 bubble trail mixed suffix is framed atomically and remains visual deferred",
          "[e19][runtime-replay][temp-entity][transaction]") {
    const std::vector<std::byte> trail{std::byte{23},std::byte{114},
        std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},
        std::byte{8},std::byte{0},std::byte{16},std::byte{0},std::byte{24},std::byte{0},
        std::byte{32},std::byte{0},std::byte{7},std::byte{0},std::byte{1},std::byte{64},std::byte{0}};
    auto init=initialization();auto host=init.game_client;host->set_movement_audio_time_origin(100);
    client::ClientWorldState world;auto replay=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(replay);
    auto bytes=trail;bytes.push_back(std::byte{255});auto bad=record(bytes,300,66001,1);
    bad.received_at=goldsrc::SoundTime{}+std::chrono::seconds{101};const auto before=world.runtime_observation();
    CHECK_FALSE(replay.session->apply_record(bad));CHECK(world.runtime_observation()==before);
    CHECK(host->drain_temporary_effects(1).statistics.received==0);
    bytes=trail;bytes.push_back(std::byte{1});auto good=record(bytes,300,66001,1);good.received_at=bad.received_at;
    const auto committed=replay.session->apply_record(good);REQUIRE(committed);REQUIRE(committed.event->temporary_effects.size()==1);
    CHECK(committed.event->temporary_effects[0].kind==client::TemporaryEffectKind::bubble_trail);
    const auto batch=host->drain_temporary_effects(1);CHECK(batch.count==0);CHECK(batch.statistics.deferred_kind==1);
}

TEST_CASE("E30 server decal observations cross atomic replay with exact wire identities",
          "[e30][goldsrc][runtime-replay][temp-entity][transaction][no-stock]") {
    struct Decal {
        std::vector<std::byte> bytes;
        std::uint8_t subtype;
        std::int32_t index;
        std::int16_t target;
        std::optional<std::int16_t> model;
    };
    // Independently authored literal SDK bodies. HIGH is not signed byte,
    // BSP uses shorts, and gunshot's target precedes its byte index.
    const std::array<Decal,7> decals{{
        {{std::byte{23},std::byte{116},std::byte{8},std::byte{0},
          std::byte{240},std::byte{255},std::byte{24},std::byte{0},std::byte{255}},116,255,0,{}},
        {{std::byte{23},std::byte{117},std::byte{8},std::byte{0},
          std::byte{240},std::byte{255},std::byte{24},std::byte{0},std::byte{0}},117,256,0,{}},
        {{std::byte{23},std::byte{104},std::byte{8},std::byte{0},
          std::byte{240},std::byte{255},std::byte{24},std::byte{0},
          std::byte{7},std::byte{5},std::byte{0}},104,7,5,{}},
        {{std::byte{23},std::byte{118},std::byte{8},std::byte{0},
          std::byte{240},std::byte{255},std::byte{24},std::byte{0},
          std::byte{255},std::byte{255},std::byte{255}},118,511,-1,{}},
        {{std::byte{23},std::byte{109},std::byte{8},std::byte{0},
          std::byte{240},std::byte{255},std::byte{24},std::byte{0},
          std::byte{5},std::byte{0},std::byte{11}},109,11,5,{}},
        {{std::byte{23},std::byte{13},std::byte{8},std::byte{0},
          std::byte{240},std::byte{255},std::byte{24},std::byte{0},
          std::byte{255},std::byte{255},std::byte{0},std::byte{0}},13,-1,0,{}},
        {{std::byte{23},std::byte{13},std::byte{8},std::byte{0},
          std::byte{240},std::byte{255},std::byte{24},std::byte{0},
          std::byte{44},std::byte{1},std::byte{5},std::byte{0},
          std::byte{52},std::byte{18}},13,300,5,std::int16_t{4660}},
    }};
    auto init=initialization();
    init.game_client.reset(); // Protocol observations need no HL implementation.
    client::ClientWorldState world;
    auto replay=goldsrc::RuntimeReplaySession::initialize(init,world);
    REQUIRE(replay);
    auto bytes=std::vector<std::byte>{std::byte{1}};
    std::vector<std::uint32_t> offsets;
    for(const auto& decal:decals) {
        offsets.push_back(static_cast<std::uint32_t>(bytes.size()*8U));
        bytes.insert(bytes.end(),decal.bytes.begin(),decal.bytes.end());
    }
    // A blood sprite at the same coordinates is a separate server message.
    const std::vector<std::byte> blood{std::byte{23},std::byte{115},
        std::byte{8},std::byte{0},std::byte{240},std::byte{255},std::byte{24},std::byte{0},
        std::byte{7},std::byte{0},std::byte{8},std::byte{0},std::byte{247},std::byte{3}};
    const auto blood_offset=static_cast<std::uint32_t>(bytes.size()*8U);
    bytes.insert(bytes.end(),blood.begin(),blood.end());
    bytes.push_back(std::byte{1});
    auto incoming=record(bytes,300,88001,1);
    incoming.received_at=goldsrc::SoundTime{}+std::chrono::milliseconds{101250};
    auto malformed=incoming;
    malformed.payload.bytes.back()=std::byte{255};
    const auto before=world.runtime_observation();
    CHECK_FALSE(replay.session->apply_record(malformed));
    CHECK(world.runtime_observation()==before);
    CHECK(replay.session->publication_revision()==1U);
    auto committed=replay.session->apply_record(incoming);
    REQUIRE(committed);
    REQUIRE(committed.event->temporary_effects.size()==decals.size()+1U);
    const auto retained=committed.event->temporary_effects;
    for(std::size_t i=0;i<decals.size();++i) {
        CAPTURE(i,decals[i].subtype);
        const auto& observed=retained[i];
        CHECK(observed.kind==client::TemporaryEffectKind::decal);
        CHECK(observed.decal_subtype==decals[i].subtype);
        CHECK(observed.decal_index==decals[i].index);
        CHECK(observed.decal_entity==decals[i].target);
        CHECK(observed.decal_model_reference==decals[i].model);
        CHECK(observed.origin.x==1.F);CHECK(observed.origin.y==-2.F);CHECK(observed.origin.z==3.F);
        CHECK(observed.generation==1U);CHECK(observed.record==88001U);
        CHECK(observed.record_ordinal==1U);CHECK(observed.message_bit_offset==offsets[i]);
        CHECK(observed.received_at_seconds==101.25);
    }
    CHECK(retained.back().kind==client::TemporaryEffectKind::blood_sprite);
    CHECK(retained.back().message_bit_offset==blood_offset);
    CHECK(retained.back().blood_scale==3U);
    const auto duplicate=replay.session->apply_record(incoming);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error->code==goldsrc::RuntimeReplayErrorCode::duplicate_record);
    auto fresh=record(decals.front().bytes,301,88002,2);
    fresh.received_at=incoming.received_at;
    const auto independent=replay.session->apply_record(fresh);
    REQUIRE(independent);REQUIRE(independent.event->temporary_effects.size()==1U);
    CHECK(independent.event->temporary_effects[0].record==88002U); // Same point, new legitimate decal.
    REQUIRE(replay.session->apply_record(record({std::byte{1}},302,88003,3)));
    REQUIRE_FALSE(replay.session->reset_generation(initialization(2U)));
    const auto stale=replay.session->apply_record(incoming);
    REQUIRE_FALSE(stale);CHECK(stale.error->code==goldsrc::RuntimeReplayErrorCode::generation_mismatch);
    incoming.payload.bytes.clear();replay.session.reset();
    CHECK(retained[6].decal_model_reference==std::int16_t{4660});
    CHECK(retained[3].decal_index==511);CHECK(retained[3].decal_entity==-1);
}

TEST_CASE("E30 every decal truncation rolls back before normal host delivery",
          "[e30][goldsrc][runtime-replay][temp-entity][transaction][no-stock]") {
    const std::array<std::vector<std::byte>,4> decals{{
        {std::byte{23},std::byte{116},std::byte{0},std::byte{0},std::byte{0},std::byte{0},
         std::byte{0},std::byte{0},std::byte{127}},
        {std::byte{23},std::byte{117},std::byte{0},std::byte{0},std::byte{0},std::byte{0},
         std::byte{0},std::byte{0},std::byte{128}},
        {std::byte{23},std::byte{104},std::byte{0},std::byte{0},std::byte{0},std::byte{0},
         std::byte{0},std::byte{0},std::byte{128},std::byte{5},std::byte{0}},
        {std::byte{23},std::byte{118},std::byte{0},std::byte{0},std::byte{0},std::byte{0},
         std::byte{0},std::byte{0},std::byte{255},std::byte{5},std::byte{0}},
    }};
    auto init=initialization();auto host=init.game_client;
    host->set_movement_audio_time_origin(100);
    client::ClientWorldState world;
    auto replay=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(replay);
    const auto before=world.runtime_observation();
    for(const auto& decal:decals) {
        for(std::size_t size=1;size<decal.size();++size) {
            CAPTURE(std::to_integer<unsigned>(decal[1]),size);
            auto bytes=decals.front(); // A valid prefix cannot publish early.
            bytes.insert(bytes.end(),decal.begin(),decal.begin()+static_cast<std::ptrdiff_t>(size));
            auto bad=record(bytes,300,89001,1);
            bad.received_at=goldsrc::SoundTime{}+std::chrono::seconds{101};
            const auto rejected=replay.session->apply_record(bad);REQUIRE_FALSE(rejected);
            REQUIRE(rejected.error->control_error);
            CHECK(rejected.error->control_error->code==goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
            CHECK(world.runtime_observation()==before);
            CHECK(host->temporary_delivery_statistics(client::TemporaryEffectKind::decal).received==0U);
        }
    }
    std::vector<std::byte> bytes;
    for(const auto& decal:decals)bytes.insert(bytes.end(),decal.begin(),decal.end());
    auto incoming=record(bytes,300,89001,1);
    incoming.received_at=goldsrc::SoundTime{}+std::chrono::seconds{101};
    REQUIRE(replay.session->apply_record(incoming));
    auto delivered=host->temporary_delivery_statistics(client::TemporaryEffectKind::decal);
    CHECK(delivered.received==4U);CHECK(delivered.forwarded==4U);CHECK(delivered.clock_dropped==0U);
    CHECK_FALSE(replay.session->apply_record(incoming));
    delivered=host->temporary_delivery_statistics(client::TemporaryEffectKind::decal);
    CHECK(delivered.received==4U);CHECK(delivered.forwarded==4U);
    host->cancel_uncommitted(); // Focus hook cannot re-publish an already committed server event.
    REQUIRE(replay.session->apply_record(record({std::byte{1}},301,89002,2)));
    CHECK(host->temporary_delivery_statistics(client::TemporaryEffectKind::decal).received==4U);
}
} // namespace
