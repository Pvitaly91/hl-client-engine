#include <hlclient/goldsrc/packet_entity_decoder.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>

#include "delta_test_fixture.hpp"
#include "player_origin_test_fixture.hpp"

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

TEST_CASE("E22 mixed player custom beam ordinary records retain their negotiated member semantics","[e22][packet-entities][transactional][no-stock]") {
    const fixture::Field ordinary[]{ {"origin[2]",0x80000004U,8,18,32000,4000},{"angles[0]",0x10U,12,16},{"modelindex",8,24,10},{"skin",0x80000002U,36,9} };
    const fixture::Field custom[]{
        {"rendermode",8,32,8},{"origin[0]",0x80000004U,0,17,32000,4000},{"origin[1]",0x80000004U,4,17,32000,4000},{"origin[2]",0x80000004U,8,17,32000,4000},
        {"angles[0]",0x80000004U,12,17,32000,4000},{"angles[1]",0x80000004U,16,17,32000,4000},{"angles[2]",0x80000004U,20,17,32000,4000},
        {"sequence",8,28,16},{"skin",8,36,16},{"modelindex",8,24,16},{"scale",4,40,8,4000,400},{"body",8,44,8},
        {"rendercolor.r",1,48,8},{"rendercolor.g",1,49,8},{"rendercolor.b",1,50,8},{"renderfx",8,52,8},{"renderamt",8,56,8},{"frame",4,60,8},{"animtime",4,64,8,4000,400}};
    goldsrc::DeltaSchemaRegistryBuilder registry;
    for(const auto& bytes:std::array{fixture::schema("entity_state_player_t",ordinary),fixture::schema("entity_state_t",ordinary),fixture::schema("custom_entity_state_t",custom)}) {
        const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(bytes,0);REQUIRE(parsed);REQUIRE(registry.insert(*parsed.schema));}
    const auto schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(registry).publish());
    const auto baselines=hlclient::test::player_origin_fixture::baselines(*schemas);
    for(unsigned type=0;type<4;++type) {
        fixture::BitWriter w;w.write(7,8);w.write(std::bit_cast<std::uint32_t>(30.F),32);w.write(40,8);w.write(3,16);
        w.write(1,1);w.write(0,1);w.write(0,1);w.write(1,3);w.write(15,8);
        hlclient::test::player_origin_fixture::coordinate(w,-12,8,18);w.write(16384,16);w.write(7,10);w.write(511,9);
        w.write(0,1);w.write(0,1);w.write(2,6);w.write(1,1);w.write(1,1);w.write(1,6);
        w.write(3,3);w.write(0xf7,8);w.write(0xff,8);w.write(7,8);w.write(type|16,8);
        for(const double value:{100.,20.,-100.,-200.,-300.})hlclient::test::player_origin_fixture::coordinate(w,value,8,17);
        w.write(0,16);w.write(4097,16);w.write(75,16);w.write(40,8);w.write(20,8);w.write(50,8);w.write(50,8);w.write(125,8);w.write(0,8);w.write(255,8);w.write(0,8);w.write(50,8);
        w.write(1,1);w.write(0,1);w.write(1,1);w.write(1,6);w.write(1,3);w.write(14,8);w.write(16384,16);w.write(7,10);w.write(0,9);
        w.write(0,16);w.align_zero();
        for(const bool malformed:{false,true}) {
            hlclient::client::ClientWorldState world;goldsrc::RuntimeReplayInitialization init;init.generation=1;init.max_clients=2;init.schemas=schemas;init.baselines=baselines;
            const auto made=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(made);const auto before=world.runtime_observation();
            goldsrc::RuntimeReplayRecord record;record.generation=1;record.record_identity=record.record_ordinal=1;record.payload=owning_payload(w.bytes(),20);
            if(malformed)record.payload.bytes.push_back(std::byte{254});record.initial_cursor=*goldsrc::StockRuntimeSourceCursor::create(0,0,record.payload.bytes.size());
            const auto result=made.session->apply_record(record);INFO((result.error ? result.error->context : "ready"));
            if(malformed){CHECK_FALSE(result);CHECK(world.runtime_observation()==before);continue;}
            REQUIRE(result);REQUIRE(world.runtime_observation()->packet_entities.size()==3);
            const auto& beam=world.runtime_observation()->packet_entities[1];REQUIRE(beam.custom_beam);CHECK_FALSE(beam.ordinary_visual_schema);
            CHECK(beam.origin.z== -12);CHECK(beam.angles.x== -100);CHECK(beam.custom_beam->end_reference.entity==1);CHECK(beam.custom_beam->end_reference.attachment==1);
            CHECK(beam.custom_beam->width_units==4);CHECK(beam.custom_beam->scroll_rate==5);CHECK(beam.custom_beam->noise_raw==20);
            CHECK(beam.custom_beam->supported==(type!=3));const auto& normal=world.runtime_observation()->packet_entities[2];
            CHECK(normal.ordinary_visual_schema);CHECK_FALSE(normal.custom_beam);CHECK(normal.origin.z== -12);CHECK(normal.angles.x==90);CHECK(normal.model_index==7);CHECK(normal.skin==0);
        }
    }
}
TEST_CASE("E13 full normal corpse record inherits a player base through its own schema",
          "[e13-respawn][goldsrc][packet-entities][transactional]") {
    const fixture::Field player_fields[]{
        {"origin[2]",0x80000004U,0U,18U,128000U,4000U},
        {"frame",4U,4U,8U}, {"sequence",8U,8U,8U}, {"modelindex",8U,12U,10U},
        {"renderfx",8U,16U,8U}, {"renderamt",8U,20U,8U}, {"gaitsequence",8U,24U,8U}};
    const fixture::Field ordinary_fields[]{
        {"frame",4U,4U,10U,16000U,4000U}, {"modelindex",8U,12U,10U},
        {"origin[2]",0x80000004U,0U,16U,32000U,4000U}, {"sequence",8U,8U,8U},
        {"renderfx",8U,16U,8U}, {"renderamt",8U,20U,8U}, {"endpos[0]",4U,28U,13U}};
    goldsrc::DeltaSchemaRegistryBuilder registry;
    for(const auto& wire:std::array{fixture::schema("entity_state_player_t",player_fields),
            fixture::schema("entity_state_t",ordinary_fields),
            fixture::schema("custom_entity_state_t",player_fields)}) {
        const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(wire,0);REQUIRE(parsed);
        REQUIRE(registry.insert(*parsed.schema));
    }
    const auto schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(registry).publish());
    const auto baselines=hlclient::test::player_origin_fixture::baselines(*schemas);
    fixture::BitWriter writer;
    writer.write(7U,8U);writer.write(std::bit_cast<std::uint32_t>(30.0F),32U);
    writer.write(40U,8U);writer.write(2U,16U);
    writer.write(1U,1U);writer.write(0U,1U);writer.write(0U,1U); // entity 1, normal, null base
    writer.write(1U,3U);writer.write(0x7fU,8U);
    hlclient::test::player_origin_fixture::coordinate(writer,-1660.0,32U,18U);
    writer.write(128U,8U);writer.write(2U,8U);writer.write(7U,10U);
    writer.write(0U,8U);writer.write(0U,8U);writer.write(0U,8U);
    writer.write(0U,1U);writer.write(0U,1U);writer.write(2U,6U); // entity 3
    writer.write(0U,1U);writer.write(1U,1U);writer.write(1U,6U); // normal, preceding base
    writer.write(1U,3U);writer.write(0x30U,8U);writer.write(17U,8U);writer.write(1U,8U);
    writer.write(0U,16U);writer.align_zero();writer.write(5U,8U);writer.write(2U,16U);
    for(const bool malformed:std::array{false,true}) {
        auto payload=owning_payload(writer.bytes(),20U);
        if(malformed) payload.bytes.push_back(std::byte{254});
        goldsrc::PacketEntitySnapshotState state{1U,2U,schemas,baselines};
        const auto result=goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
            {payload,*goldsrc::StockRuntimeSourceCursor::create(0,0,payload.bytes.size()),1U,1U},state);
        INFO((result.error ? result.error->context : "decoded"));
        if(malformed) {CHECK_FALSE(result);CHECK_FALSE(state.current_snapshot());continue;}
        REQUIRE(result);REQUIRE(state.current_snapshot());
        const auto* corpse=state.current_snapshot()->find_exact(3U);REQUIRE(corpse);
        CHECK(corpse->schema_category()==goldsrc::EntitySchemaCategory::ordinary_entity);
        CHECK(corpse->state_base_reference().kind()==goldsrc::EntityStateBaseReferenceKind::intra_message_entity);
        CHECK(std::get<double>(corpse->object().find_exact("origin[2]")->value())== -1660.0);
        CHECK(std::get<double>(corpse->object().find_exact("frame")->value())==128.0);
        CHECK(std::get<double>(corpse->object().find_exact("endpos[0]")->value())==0.0);
        CHECK(std::get<std::uint32_t>(corpse->object().find_exact("renderfx")->value())==17U);
        CHECK(std::get<std::uint32_t>(corpse->object().find_exact("renderamt")->value())==1U);
        REQUIRE(state.control_state().view_entity());
        CHECK(state.control_state().view_entity()->wire_entity_reference==2);
        hlclient::client::ClientWorldState world;
        goldsrc::RuntimeReplayInitialization init;init.generation=1;init.max_clients=2;
        init.schemas=schemas;init.baselines=baselines; // protocol-only, no HL fallback
        const auto replay=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(replay);
        goldsrc::RuntimeReplayRecord record;record.generation=1;record.record_identity=1;record.record_ordinal=1;
        record.payload=payload;record.initial_cursor=*goldsrc::StockRuntimeSourceCursor::create(0,0,payload.bytes.size());
        REQUIRE(replay.session->apply_record(record));
        const auto& observed=world.runtime_observation()->packet_entities.back();
        CHECK(observed.entity_number==3U);CHECK(observed.render_effect==17U);CHECK(observed.render_amount==1U);
        CHECK(observed.origin.z== -1660.0);CHECK(observed.frame==128.0);
        CHECK_FALSE(replay.session->apply_record(record)); // duplicate, no second publication
    }
    for(const unsigned mode:std::array{0U,1U,2U}) {
        auto selected_schemas=schemas;
        if(mode!=0U) {
            auto fields=std::vector<fixture::Field>{std::begin(ordinary_fields),std::end(ordinary_fields)};
            if(mode==1U) fields[2].offset=40U; else fields[2].type=0x80000008U;
            goldsrc::DeltaSchemaRegistryBuilder changed;
            for(const auto& wire:std::array{fixture::schema("entity_state_player_t",player_fields),
                    fixture::schema("entity_state_t",fields)}) {
                const auto parsed=goldsrc::DeltaDescriptionParser{}.parse(wire,0);REQUIRE(parsed);
                REQUIRE(changed.insert(*parsed.schema));
            }
            selected_schemas=std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(changed).publish());
        }
        auto payload=owning_payload(writer.bytes(),20U);
        if(mode==0U) payload.bytes[8U]|=std::byte{2U}; // prior record uses custom entity state
        goldsrc::PacketEntitySnapshotState state{1U,2U,selected_schemas,
            hlclient::test::player_origin_fixture::baselines(*selected_schemas)};
        const auto result=goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
            {payload,*goldsrc::StockRuntimeSourceCursor::create(0,0,payload.bytes.size()),1U,1U},state);
        REQUIRE_FALSE(result);REQUIRE(result.error);
        CHECK(result.error->code==goldsrc::PacketEntityDecodeErrorCode::schema_mismatch);
        CHECK_FALSE(state.current_snapshot());CHECK_FALSE(state.control_state().view_entity());
        hlclient::client::ClientWorldState world;
        goldsrc::RuntimeReplayInitialization init;init.generation=1;init.max_clients=2;
        init.schemas=selected_schemas;init.baselines=hlclient::test::player_origin_fixture::baselines(*selected_schemas);
        const auto replay=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(replay);
        const auto before=world.runtime_observation();
        goldsrc::RuntimeReplayRecord record;record.generation=1;record.record_identity=1;record.record_ordinal=1;
        record.payload=payload;record.initial_cursor=*goldsrc::StockRuntimeSourceCursor::create(0,0,payload.bytes.size());
        const auto rejected=replay.session->apply_record(record);REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
        CHECK(world.runtime_observation()==before);
        CHECK(rejected.error->decoder_entity_number==3U);
        CHECK(rejected.error->decoder_schema_boundary==(mode==0U ? "intra_category" : "intra_normal_member"));
        const auto summary=goldsrc::runtime_failure_summary(*rejected.error);
        CHECK(summary.find("packet_entity=3")!=std::string::npos);
        CHECK(summary.find(mode==0U ? "packet_schema_boundary=intra_category" :
            "packet_schema_boundary=intra_normal_member")!=std::string::npos);
    }
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

// Owned descriptor/wire reproduction of retained signon slot reuse, not a
// captured stock packet. Common members deliberately differ in order, wire
// precision, and (for angles/skin) numeric representation.
[[nodiscard]] std::shared_ptr<const goldsrc::DeltaSchemaRegistryState>
baseline_reuse_schemas(const unsigned incompatible_member = 0U)
{
    fixture::Field ordinary[]{
        {"origin[0]",0x80000004U,0U,18U,32000U,4000U},
        {"angles[0]",0x10U,12U,16U}, {"modelindex",8U,24U,10U},
        {"skin",0x80000002U,36U,9U}, {"frame",4U,60U,10U,16000U,4000U},
        {"rendermode",8U,32U,8U}, {"endpos[0]",4U,68U,13U}};
    fixture::Field custom[]{
        {"rendermode",8U,32U,8U}, {"skin",8U,36U,16U},
        {"angles[0]",0x80000004U,12U,17U,32000U,4000U},
        {"origin[0]",0x80000004U,0U,17U,32000U,4000U},
        {"modelindex",8U,24U,16U}, {"frame",4U,60U,8U},
        {"scale",4U,40U,8U}};
    if (incompatible_member == 1U) custom[3U].offset = 4U;
    if (incompatible_member == 2U) custom[3U].type = 0x80000008U;
    if (incompatible_member == 3U) custom[0U].type = 1U; // BYTE is not the beam INTEGER grammar
    if (incompatible_member == 4U) ordinary[0U].offset = 4U;
    goldsrc::DeltaSchemaRegistryBuilder builder;
    for (const auto& wire : std::array{
            fixture::schema("entity_state_t", ordinary),
            fixture::schema("entity_state_player_t", ordinary),
            fixture::schema("custom_entity_state_t", custom)}) {
        const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(wire, 0U);
        REQUIRE(parsed);
        REQUIRE(builder.insert(*parsed.schema));
    }
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

[[nodiscard]] std::shared_ptr<const goldsrc::EntityBaselineRegistryState>
baseline_reuse_baselines(const goldsrc::DeltaSchemaRegistryState& schemas,
                         const bool custom, const std::uint64_t generation = 1U)
{
    fixture::BitWriter writer;
    writer.write(31U, 11U);
    writer.write(custom ? 2U : 0U, 2U);
    writer.write(1U, 3U);
    writer.write(0x7fU, 8U);
    if (custom) {
        writer.write(16U, 8U);
        writer.write(4097U, 16U); // packed endpoint, wider than ordinary skin
        hlclient::test::player_origin_fixture::coordinate(writer, 90.0, 8U, 17U);
        hlclient::test::player_origin_fixture::coordinate(writer, -12.0, 8U, 17U);
        writer.write(7U, 16U);
        writer.write(12U, 8U);
        writer.write(6U, 8U);
    } else {
        hlclient::test::player_origin_fixture::coordinate(writer, -12.0, 8U, 18U);
        writer.write(16384U, 16U); // angle 90 degrees
        writer.write(7U, 10U);
        writer.write(3U, 9U); // signed-magnitude SHORT -1 -> custom uint16 65535
        writer.write(50U, 10U); // frame 12.5
        writer.write(16U, 8U);
        writer.write(75U, 13U);
    }
    writer.write(goldsrc::kGoldSrcBaselineTerminator, 16U);
    writer.write(0U, 6U); // no instanced baselines
    writer.align_zero();
    auto payload = owning_payload(writer.bytes(), 10U);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, payload.bytes.size());
    REQUIRE(cursor);
    const auto decoded = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        {&payload, *cursor, 0U, generation, 1U, "entity_state_t",
         "entity_state_player_t", "custom_entity_state_t", writer.bit_offset()}, schemas);
    REQUIRE(decoded);
    REQUIRE(decoded.registry);
    REQUIRE(decoded.entity_count == 1U);
    REQUIRE(decoded.instanced_count == 0U);
    return std::make_shared<const goldsrc::EntityBaselineRegistryState>(
        std::move(*decoded.registry));
}

[[nodiscard]] std::vector<std::byte> baseline_reuse_full(
    const bool custom, const bool replace_skin = false,
    const bool truncated_replacement = false, const float time = 30.0F)
{
    fixture::BitWriter writer;
    prefix(writer, time, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 1U);
    writer.write(0U, 1U); // nonsequential full entity number
    writer.write(0U, 1U); // relative number
    writer.write(31U, 6U);
    writer.write(custom ? 1U : 0U, 1U);
    // Instanced flag is absent because the retained table has no alternate slot.
    writer.write(0U, 1U); // no intra-message base: exact signon identity 31
    writer.write(replace_skin ? 1U : 0U, 3U);
    if (replace_skin) {
        REQUIRE_FALSE(custom);
        writer.write(0x08U, 8U); // selected ordinary skin is completely replaced
        if (truncated_replacement) {
            writer.align_zero(); // fewer than nine skin bits remain; no terminator
            return writer.bytes();
        }
        writer.write(34U, 9U); // signed-magnitude 17
    }
    finish_entities(writer);
    return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> baseline_reuse_delta(
    const bool custom, const std::uint8_t base_tag, const float time = 30.02F)
{
    fixture::BitWriter writer;
    prefix(writer, time, goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode, 1U, base_tag);
    writer.write(0U, 1U); // update, not removal
    writer.write(0U, 1U); // relative number
    writer.write(31U, 6U);
    writer.write(custom ? 1U : 0U, 1U);
    writer.write(1U, 3U);
    writer.write(custom ? 0x10U : 0x04U, 8U); // modelindex in the selected schema
    writer.write(42U, custom ? 16U : 10U);
    finish_entities(writer);
    return writer.bytes();
}

[[nodiscard]] goldsrc::RuntimeReplayRecord baseline_reuse_record(
    const std::vector<std::byte>& bytes, const std::uint32_t sequence,
    const std::size_t ordinal)
{
    goldsrc::RuntimeReplayRecord record;
    record.generation = 1U;
    record.record_identity = ordinal;
    record.record_ordinal = ordinal;
    record.payload = owning_payload(bytes, sequence);
    record.initial_cursor = *goldsrc::StockRuntimeSourceCursor::create(
        0U, 0U, record.payload.bytes.size());
    return record;
}

} // namespace

TEST_CASE("E26 retained signon entity slot projects full normal and custom records atomically",
          "[e26][packet-entities][baseline-reuse][no-stock]")
{
    for (const bool baseline_custom : {false, true}) {
        CAPTURE(baseline_custom);
        const auto schemas = baseline_reuse_schemas();
        const auto baselines = baseline_reuse_baselines(*schemas, baseline_custom);
        const auto* retained = baselines->find_exact(goldsrc::EntityBaselineKey::for_entity(31U));
        REQUIRE(retained);
        CHECK(retained->schema_category() == (baseline_custom
            ? goldsrc::EntitySchemaCategory::custom_entity
            : goldsrc::EntitySchemaCategory::ordinary_entity));
        hlclient::client::ClientWorldState world;
        goldsrc::RuntimeReplayInitialization initialization;
        initialization.generation = 1U;
        initialization.max_clients = 1U;
        initialization.schemas = schemas;
        initialization.baselines = baselines;
        const auto replay = goldsrc::RuntimeReplaySession::initialize(initialization, world);
        REQUIRE(replay);
        CHECK_FALSE(initialization.game_client); // no module, resources, or stock assets
        const bool current_custom = !baseline_custom;
        const auto applied = replay.session->apply_record(baseline_reuse_record(
            baseline_reuse_full(current_custom, baseline_custom), 100U, 1U));
        INFO((applied.error ? applied.error->context : "retained baseline projected"));
        REQUIRE(applied);
        const auto first = replay.session->decoder_state().current_snapshot();
        REQUIRE(first);
        const auto* entity = first->find_exact(31U);
        REQUIRE(entity);
        CHECK(entity->schema_category() == (current_custom
            ? goldsrc::EntitySchemaCategory::custom_entity
            : goldsrc::EntitySchemaCategory::ordinary_entity));
        CHECK(entity->object().schema_name() == (current_custom
            ? "custom_entity_state_t" : "entity_state_t"));
        CHECK(entity->state_base_reference().kind() ==
            goldsrc::EntityStateBaseReferenceKind::entity_baseline);
        CHECK(entity->baseline_key() == goldsrc::EntityBaselineKey::for_entity(31U));
        CHECK(hlclient::test::player_origin_fixture::value(*first, 31U, "origin[0]") == -12.0);
        CHECK(hlclient::test::player_origin_fixture::value(*first, 31U, "angles[0]") == 90.0);
        CHECK(hlclient::test::player_origin_fixture::value(*first, 31U, "frame") ==
            (baseline_custom ? 12.0 : 12.5));
        CHECK(std::get<std::uint32_t>(entity->object().find_exact("modelindex")->value()) == 7U);
        CHECK(std::get<std::uint32_t>(entity->object().find_exact("rendermode")->value()) == 16U);
        if (current_custom) {
            CHECK(std::get<std::uint32_t>(entity->object().find_exact("skin")->value()) == 65535U);
            CHECK(hlclient::test::player_origin_fixture::value(*first, 31U, "scale") == 0.0);
            CHECK_FALSE(entity->object().find_exact("endpos[0]"));
        } else {
            CHECK(std::get<std::int32_t>(entity->object().find_exact("skin")->value()) == 17);
            CHECK(hlclient::test::player_origin_fixture::value(*first, 31U, "endpos[0]") == 0.0);
            CHECK_FALSE(entity->object().find_exact("scale"));
        }
        REQUIRE(world.runtime_observation());
        REQUIRE(world.runtime_observation()->packet_entities.size() == 1U);
        const auto& observed = world.runtime_observation()->packet_entities.front();
        CHECK(observed.entity_number == 31U);
        CHECK(observed.origin.x == -12.0);
        CHECK(observed.angles.x == 90.0);
        CHECK(observed.custom_beam.has_value() == current_custom);
        CHECK(observed.ordinary_visual_schema == !current_custom);

        REQUIRE(replay.session->apply_record(baseline_reuse_record(
            baseline_reuse_delta(current_custom, 100U), 101U, 2U)));
        const auto updated = replay.session->decoder_state().current_snapshot();
        REQUIRE(updated);
        const auto* changed = updated->find_exact(31U);
        REQUIRE(changed);
        CHECK(changed->state_base_reference().kind() ==
            goldsrc::EntityStateBaseReferenceKind::previous_snapshot_entity);
        CHECK(std::get<std::uint32_t>(changed->object().find_exact("modelindex")->value()) == 42U);
        CHECK(std::get<std::uint32_t>(entity->object().find_exact("modelindex")->value()) == 7U);

        const auto before_world = world.runtime_observation();
        const auto before_history = replay.session->decoder_state().history().snapshot_count();
        const auto before_bytes = replay.session->decoder_state().history().accounted_value_bytes();
        const auto before_controls = replay.session->decoder_state().control_state();
        const auto before_revision = replay.session->publication_revision();
        // Re-run the category-changing projection before a malformed suffix;
        // rollback must cover this new path as well as the staged server time.
        auto malformed = baseline_reuse_full(current_custom, baseline_custom, false, 31.0F);
        malformed.push_back(std::byte{254U});
        const auto rejected = replay.session->apply_record(baseline_reuse_record(malformed, 102U, 3U));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->decoder_error == goldsrc::PacketEntityDecodeErrorCode::unsupported_opcode);
        CHECK(world.runtime_observation() == before_world);
        CHECK(replay.session->publication_revision() == before_revision);
        CHECK(replay.session->decoder_state().current_snapshot() == updated);
        CHECK(replay.session->decoder_state().history().snapshot_count() == before_history);
        CHECK(replay.session->decoder_state().history().accounted_value_bytes() == before_bytes);
        CHECK(replay.session->decoder_state().control_state() == before_controls);

        // A later full packet switches back, using the immutable signon slot,
        // not the current custom/ordinary snapshot or its delta-modified model.
        REQUIRE(replay.session->apply_record(baseline_reuse_record(
            baseline_reuse_full(baseline_custom, false, false, 31.0F), 102U, 3U)));
        const auto repeated = replay.session->decoder_state().current_snapshot();
        REQUIRE(repeated);
        const auto* restored = repeated->find_exact(31U);
        REQUIRE(restored);
        CHECK(restored->schema_category() == retained->schema_category());
        CHECK(restored->state_base_reference().kind() ==
            goldsrc::EntityStateBaseReferenceKind::entity_baseline);
        CHECK(std::get<std::uint32_t>(restored->object().find_exact("modelindex")->value()) == 7U);
        CHECK(std::get<std::uint32_t>(retained->object().find_exact("modelindex")->value()) == 7U);
        CHECK(baselines->baseline_count() == 1U);
        CHECK_FALSE(baselines->find_exact(goldsrc::EntityBaselineKey::for_alternate_slot(0U)));
        CHECK(replay.session->decoder_state().history().snapshot_count() == before_history + 1U);
    }
}

TEST_CASE("E26 retained custom skin narrowing requires a complete replacement",
          "[e26][packet-entities][baseline-reuse][no-stock]")
{
    const auto schemas = baseline_reuse_schemas();
    const auto baselines = baseline_reuse_baselines(*schemas, true);
    goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
    goldsrc::GoldSrcPacketEntityDecoder decoder;
    const auto apply = [&](const std::vector<std::byte>& bytes, const std::uint32_t sequence) {
        auto payload = owning_payload(bytes, sequence);
        return decoder.decode_and_apply({payload, {}, 1U, sequence}, state);
    };
    REQUIRE(apply(baseline_reuse_full(true), 100U));
    const auto before = state.current_snapshot();
    const auto controls = state.control_state();
    const auto bytes = state.history().accounted_value_bytes();
    for (const bool selected_but_truncated : {false, true}) {
        CAPTURE(selected_but_truncated);
        const auto rejected = apply(baseline_reuse_full(
            false, selected_but_truncated, selected_but_truncated), 101U);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == (selected_but_truncated
            ? goldsrc::PacketEntityDecodeErrorCode::delta_decode_failed
            : goldsrc::PacketEntityDecodeErrorCode::schema_mismatch));
        CHECK(state.current_snapshot() == before);
        CHECK(state.control_state() == controls);
        CHECK(state.history().snapshot_count() == 1U);
        CHECK(state.history().accounted_value_bytes() == bytes);
    }
    REQUIRE(apply(baseline_reuse_full(false, true), 101U));
    const auto* narrowed = state.current_snapshot()->find_exact(31U);
    REQUIRE(narrowed);
    CHECK(std::get<std::int32_t>(narrowed->object().find_exact("skin")->value()) == 17);
    const auto strict_before = state.current_snapshot();
    const auto category_change = apply(baseline_reuse_delta(true, 101U), 102U);
    REQUIRE_FALSE(category_change);
    REQUIRE(category_change.error);
    CHECK(category_change.error->code == goldsrc::PacketEntityDecodeErrorCode::schema_mismatch);
    CHECK(state.current_snapshot() == strict_before);
}

TEST_CASE("E26 retained entity baseline projection preserves descriptor and generation boundaries",
          "[e26][packet-entities][baseline-reuse][no-stock]")
{
    for (const unsigned incompatible_member : {1U, 2U, 3U, 4U}) {
        CAPTURE(incompatible_member);
        const auto schemas = baseline_reuse_schemas(incompatible_member);
        // Same-category projection cannot rescue a stale retained descriptor.
        const auto source_schemas = incompatible_member == 4U
            ? baseline_reuse_schemas() : schemas;
        const auto baselines = baseline_reuse_baselines(*source_schemas, false);
        goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, baselines};
        REQUIRE(state.valid());
        auto payload = owning_payload(baseline_reuse_full(incompatible_member != 4U), 100U);
        const auto rejected = goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
            {payload, {}, 1U, 1U}, state);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == goldsrc::PacketEntityDecodeErrorCode::schema_mismatch);
        CHECK(rejected.error->entity_number == 31U);
        CHECK_FALSE(state.current_snapshot());
        CHECK(state.history().snapshot_count() == 0U);
        CHECK_FALSE(state.control_state().server_time());
    }
    // Unsupported storage widths cannot enter the validated schema registry;
    // reject that malformed negotiated descriptor before baseline projection.
    fixture::BitWriter storage_wire;
    storage_wire.write(goldsrc::kDeltaDescriptionOpcode, 8U);
    storage_wire.string("custom_entity_state_t");
    storage_wire.write(1U, 16U);
    storage_wire.write(1U, 3U);
    storage_wire.write(0x7bU, 8U); // offset zero omitted
    storage_wire.write(0x80000004U, 32U);
    storage_wire.string("origin[0]");
    storage_wire.write(2U, 8U); // storage metadata must equal one
    storage_wire.write(17U, 8U);
    storage_wire.write(32000U, 32U);
    storage_wire.write(4000U, 32U);
    storage_wire.align_zero();
    const auto invalid_storage = goldsrc::DeltaDescriptionParser{}.parse(storage_wire.bytes(), 0U);
    REQUIRE_FALSE(invalid_storage);
    REQUIRE(invalid_storage.error);
    CHECK(invalid_storage.error->code == goldsrc::DeltaDescriptionErrorCode::invalid_storage_size);

    const auto schemas = baseline_reuse_schemas();
    const auto old_baselines = baseline_reuse_baselines(*schemas, false);
    const auto new_baselines = baseline_reuse_baselines(*schemas, false, 2U);
    {
        const auto* ordinary = old_baselines->find_exact(goldsrc::EntityBaselineKey::for_entity(31U));
        REQUIRE(ordinary);
        goldsrc::EntityBaselineRegistryBuilder builder{
            *schemas, {}, goldsrc::EntitySnapshotCompatibilityProfile::public_goldsrc48_entity_delta_v1};
        REQUIRE(builder.insert(goldsrc::EntityBaselineKey::for_alternate_slot(0U),
            goldsrc::EntitySchemaCategory::alternate_explicit_schema,
            ordinary->object(), ordinary->source_geometry()));
        const auto published = std::move(builder).publish();
        REQUIRE(published);
        auto alternate = std::make_shared<const goldsrc::EntityBaselineRegistryState>(
            std::move(*published.state));
        goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, alternate};
        REQUIRE(state.valid());
        const auto alternate_full = [](const bool custom) {
            fixture::BitWriter writer;
            prefix(writer, 30.0F, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 1U);
            writer.write(0U, 1U); // nonsequential
            writer.write(0U, 1U); // relative number
            writer.write(31U, 6U);
            writer.write(custom ? 1U : 0U, 1U);
            writer.write(1U, 1U); // explicit alternate baseline
            writer.write(0U, 6U); // exact retained slot zero, no intra-message flag
            writer.write(0U, 3U); // no changed fields
            finish_entities(writer);
            return writer.bytes();
        };
        auto normal = owning_payload(alternate_full(false), 100U);
        REQUIRE(goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
            {normal, {}, 1U, 1U}, state));
        const auto before = state.current_snapshot();
        REQUIRE(before);
        CHECK(before->find_exact(31U)->state_base_reference().kind() ==
            goldsrc::EntityStateBaseReferenceKind::instanced_baseline);
        const auto controls = state.control_state();
        const auto bytes = state.history().accounted_value_bytes();
        auto custom = owning_payload(alternate_full(true), 101U);
        const auto rejected = goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
            {custom, {}, 1U, 2U}, state);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code == goldsrc::PacketEntityDecodeErrorCode::schema_mismatch);
        CHECK(rejected.error->entity_number == 31U);
        CHECK(state.current_snapshot() == before);
        CHECK(state.control_state() == controls);
        CHECK(state.history().snapshot_count() == 1U);
        CHECK(state.history().accounted_value_bytes() == bytes);
    }
    auto payload = owning_payload(baseline_reuse_full(true), 100U);
    goldsrc::PacketEntitySnapshotState wrong_domain{1U, 1U, schemas, new_baselines};
    CHECK_FALSE(wrong_domain.valid());
    const auto invalid_state = goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
        {payload, {}, 1U, 1U}, wrong_domain);
    REQUIRE_FALSE(invalid_state);
    REQUIRE(invalid_state.error);
    CHECK(invalid_state.error->code == goldsrc::PacketEntityDecodeErrorCode::invalid_configuration);
    CHECK_FALSE(wrong_domain.current_snapshot());

    goldsrc::PacketEntitySnapshotState state{1U, 1U, schemas, old_baselines};
    const auto mismatch = goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
        {payload, {}, 2U, 1U}, state);
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error);
    CHECK(mismatch.error->code == goldsrc::PacketEntityDecodeErrorCode::source_generation_mismatch);
    CHECK_FALSE(state.current_snapshot());
    CHECK_FALSE(state.reset_source_generation(2U, 1U, schemas, old_baselines));
    CHECK(state.source_generation() == 1U);
    REQUIRE(state.reset_source_generation(2U, 1U, schemas, new_baselines));
    const auto stale = goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
        {payload, {}, 1U, 1U}, state);
    REQUIRE_FALSE(stale);
    REQUIRE(stale.error);
    CHECK(stale.error->code == goldsrc::PacketEntityDecodeErrorCode::source_generation_mismatch);
    CHECK_FALSE(state.current_snapshot());
    CHECK(state.history().snapshot_count() == 0U);
}

TEST_CASE("E10 receiving origin is restored before full packet player inheritance",
          "[e10-origin][goldsrc][packet-entities]")
{
    namespace p = hlclient::test::player_origin_fixture;
    for(const auto receiver : {1U,2U}) {
        INFO("receiving entity=" << receiver);
        auto schemas=p::schemas();
        goldsrc::PacketEntitySnapshotState state{1U,2U,schemas,p::baselines(*schemas)};
        const p::Position local{-432.0,930.0,-1660.0}, remote{-602.5,834.0,-1660.0};
        auto source=p::payload(p::full(receiver,local,remote),100U);
        goldsrc::PacketEntityDecodeInput input{source,{},1U,1U};
        input.receiving_player_entity=receiver;
        const auto result=goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(input,state);
        REQUIRE(result);
        REQUIRE(state.current_snapshot());
        const auto& snapshot=*state.current_snapshot();
        CHECK(p::value(snapshot,receiver,"origin[0]")==local[0]);
        CHECK(p::value(snapshot,receiver,"origin[1]")==local[1]);
        CHECK(p::value(snapshot,receiver,"origin[2]")==local[2]);
        CHECK(p::value(snapshot,3U-receiver,"origin[0]")==remote[0]);
        CHECK(p::value(snapshot,3U-receiver,"origin[1]")==remote[1]);
        CHECK(p::value(snapshot,3U-receiver,"origin[2]")==remote[2]);
        CHECK(snapshot.find_exact(2U)->state_base_reference().kind()==
            goldsrc::EntityStateBaseReferenceKind::intra_message_entity);
        // A later delta may inherit from that exact immutable full snapshot.
        auto retained=p::payload(zero_delta(2U,100U,30.02F),101U);
        const auto next=goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
            {retained,{},1U,2U,"entity_state_t","entity_state_player_t","custom_entity_state_t",
             "clientdata_t","weapon_data_t",goldsrc::ClientDataReceiverMode::ordinary_game_client,{},receiver},state);
        REQUIRE(next);
        CHECK(p::value(*state.current_snapshot(),3U-receiver,"origin[2]")==remote[2]);
        CHECK(p::value(snapshot,3U-receiver,"origin[2]")==remote[2]);
        const auto before=state.current_snapshot();
        auto malformed=p::full(receiver,local,remote); malformed.push_back(std::byte{0xff});
        auto bad=p::payload(std::move(malformed),102U);
        const auto rejected=goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
            {bad,{},1U,3U,"entity_state_t","entity_state_player_t","custom_entity_state_t",
             "clientdata_t","weapon_data_t",goldsrc::ClientDataReceiverMode::ordinary_game_client,{},receiver},state);
        CHECK_FALSE(rejected);
        CHECK(state.current_snapshot()==before);
    }
}

TEST_CASE("E10 origin transfer needs exact receiver and fresh complete clientdata",
          "[e10-origin][goldsrc][packet-entities]")
{
    namespace p = hlclient::test::player_origin_fixture;
    const p::Position local{-432.03125,930.0625,-1660.03125}, remote{-602.5,834.0,-1660.0};
    for(const int mode : {0,1,2,3,4,5}) {
        INFO("availability mode=" << mode);
        const bool complete=mode!=2;
        auto schemas=p::schemas(complete);
        goldsrc::PacketEntitySnapshotState state{1U,2U,schemas,p::baselines(*schemas)};
        auto source=p::payload(p::full(1U,local,remote,true,mode!=1,complete,mode==3),100U);
        goldsrc::PacketEntityDecodeInput input{source,{},1U,1U};
        if(mode!=0) input.receiving_player_entity=mode==5 ? 3U : 1U;
        if(mode==4) input.client_receiver_mode=goldsrc::ClientDataReceiverMode::proxy_or_hltv;
        const auto result=goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(input,state);
        if(mode>=4) { CHECK_FALSE(result); CHECK_FALSE(state.current_snapshot()); continue; }
        REQUIRE(result);
        CHECK(p::value(*state.current_snapshot(),1U,"origin[2]")==0.0);
        // Explicit remote coordinates must never be replaced with local ones.
        CHECK(p::value(*state.current_snapshot(),2U,"origin[2]")==remote[2]);
    }
    auto schemas=p::schemas();
    goldsrc::PacketEntitySnapshotState state{1U,2U,schemas,p::baselines(*schemas)};
    auto fresh=p::payload(p::full(1U,local,remote),100U);
    REQUIRE(goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
        {fresh,{},1U,1U,"entity_state_t","entity_state_player_t","custom_entity_state_t",
         "clientdata_t","weapon_data_t",goldsrc::ClientDataReceiverMode::ordinary_game_client,{},1U},state));
    CHECK(p::value(*state.current_snapshot(),1U,"origin[2]")==local[2]);
    CHECK(p::value(*state.current_snapshot(),2U,"origin[2]")==remote[2]);
    auto stale=p::payload(p::full(1U,local,remote,true,false),101U);
    REQUIRE(goldsrc::GoldSrcPacketEntityDecoder{}.decode_and_apply(
        {stale,{},1U,2U,"entity_state_t","entity_state_player_t","custom_entity_state_t",
         "clientdata_t","weapon_data_t",goldsrc::ClientDataReceiverMode::ordinary_game_client,{},1U},state));
    CHECK(p::value(*state.current_snapshot(),1U,"origin[2]")==0.0);
}

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

TEST_CASE("New entities absent from signon use a distinct null baseline",
          "[goldsrc][packet-entities][dynamic][null-baseline]")
{
    PacketFixture fixture_state;
    CHECK(fixture_state.baselines->find_exact(
              goldsrc::EntityBaselineKey::for_entity(100U)) == nullptr);

    fixture::BitWriter full;
    prefix(full, 100.0F, goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 1U);
    full.write(0U, 1U);    // not sequential
    full.write(1U, 1U);    // absolute entity number
    full.write(100U, 11U);
    full.write(0U, 1U);    // ordinary schema
    full.write(0U, 1U);    // not instanced
    full.write(0U, 1U);    // no intra-message baseline
    byte_delta(full, 77U);
    finish_entities(full);

    const auto first = decode(fixture_state, full.bytes(), 250U);
    const auto& first_event = entity_event(first);
    REQUIRE(first_event.snapshot);
    CHECK(byte_value(*first_event.snapshot, 100U) == 77U);
    REQUIRE(first_event.snapshot->find_exact(100U));
    CHECK(first_event.snapshot->find_exact(100U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::null_baseline);
    CHECK(first_event.snapshot->find_exact(100U)->state_base_reference().value() ==
          100U);
    CHECK(fixture_state.baselines->find_exact(
              goldsrc::EntityBaselineKey::for_entity(100U)) == nullptr);

    fixture::BitWriter delta;
    prefix(delta, 101.0F, goldsrc::kGoldSrcSvcDeltaPacketEntitiesOpcode,
           1U, 250U);
    delta.write(0U, 1U);   // non-removal
    delta.write(1U, 1U);   // absolute entity number
    delta.write(100U, 11U);
    delta.write(0U, 1U);   // ordinary schema
    delta.write(0U, 1U);   // not instanced
    byte_delta(delta, 88U);
    finish_entities(delta);

    const auto second = decode(fixture_state, delta.bytes(), 251U);
    const auto& second_event = entity_event(second);
    REQUIRE(second_event.snapshot);
    CHECK(byte_value(*second_event.snapshot, 100U) == 88U);
    REQUIRE(second_event.snapshot->find_exact(100U));
    CHECK(second_event.snapshot->find_exact(100U)->state_base_reference().kind() ==
          goldsrc::EntityStateBaseReferenceKind::previous_snapshot_entity);
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

    SECTION("explicit intra-message offset is not a null baseline")
    {
        PacketFixture fixture_state;
        fixture::BitWriter writer;
        prefix(writer, 1.0F, goldsrc::kGoldSrcSvcPacketEntitiesOpcode,
               1U);
        writer.write(0U, 1U);   // not sequential
        writer.write(1U, 1U);   // absolute entity number
        writer.write(100U, 11U);
        writer.write(0U, 1U);   // ordinary schema
        writer.write(0U, 1U);   // not instanced
        writer.write(1U, 1U);   // explicit intra-message offset
        writer.write(1U, 6U);   // no earlier record exists
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

TEST_CASE("E32.1 completed entity body cannot own its contemporary suffix delta base",
          "[e321][e321-a-cache][goldsrc][packet-entity][history][fragment][suffix]")
{
    const auto apply = [](PacketFixture& target, const std::span<const std::byte> bytes,
                          const std::uint32_t sequence, const bool completion, const bool suffix) {
        auto payload = owning_payload(bytes,sequence);
        payload.source_reliable = completion || suffix;
        payload.reassembled = completion;
        payload.normal_fragment_completion = completion;
        payload.contemporaneous_fragment_suffix = suffix;
        const auto cursor = goldsrc::StockRuntimeSourceCursor::create(0U,0U,payload.bytes.size());
        REQUIRE(cursor);
        return target.decoder.decode_and_apply({payload,*cursor,1U,10U,"x","x","x"},target.state);
    };
    PacketFixture target;
    REQUIRE(apply(target,kFullFrameBytes,250U,true,false));
    REQUIRE(target.state.current_snapshot());
    CHECK(target.state.current_snapshot()->entity_count() == 5U);
    CHECK(target.state.history().snapshot_count() == 0U);
    const auto current_completion = target.state.current_snapshot();
    const auto duplicate = apply(target,kFullFrameBytes,250U,true,false);
    REQUIRE_FALSE(duplicate); REQUIRE(duplicate.error);
    CHECK(duplicate.error->code == goldsrc::PacketEntityDecodeErrorCode::duplicate_source_frame);
    const auto empty = empty_full(101.F);
    const auto conflict = apply(target,empty,250U,true,false);
    REQUIRE_FALSE(conflict); REQUIRE(conflict.error);
    CHECK(conflict.error->code == goldsrc::PacketEntityDecodeErrorCode::conflicting_source_frame);
    CHECK(target.state.current_snapshot() == current_completion);
    REQUIRE(apply(target,empty,250U,false,true));
    CHECK(target.state.current_snapshot()->entity_count() == 0U);
    CHECK(target.state.history().snapshot_count() == 1U);
    const auto normal_conflict = apply(target,kFullFrameBytes,250U,false,false);
    REQUIRE_FALSE(normal_conflict); REQUIRE(normal_conflict.error);
    CHECK(normal_conflict.error->code == goldsrc::PacketEntityDecodeErrorCode::conflicting_source_frame);
    const auto delta = zero_delta(0U,250U);
    REQUIRE(apply(target,delta,251U,false,false));
    CHECK(target.state.current_snapshot()->entity_count() == 0U);
    CHECK(target.state.history().snapshot_count() == 2U);
    PacketFixture no_suffix;
    REQUIRE(apply(no_suffix,kFullFrameBytes,250U,true,false));
    const auto missing = apply(no_suffix,delta,251U,false,false);
    REQUIRE_FALSE(missing); REQUIRE(missing.error);
    CHECK(missing.error->code == goldsrc::PacketEntityDecodeErrorCode::missing_delta_base);
    CHECK(no_suffix.state.history().snapshot_count() == 0U);
    REQUIRE(target.state.reset_source_generation(2U,1U,target.schemas,make_baselines(*target.schemas,2U)));
    // The reset rejects old generation input and clears both fingerprint domains.
    auto reset_payload=owning_payload(kFullFrameBytes,250U);
    reset_payload.normal_fragment_completion=true;reset_payload.source_reliable=true;reset_payload.reassembled=true;
    REQUIRE(target.decoder.decode_and_apply({reset_payload,*goldsrc::StockRuntimeSourceCursor::create(
        0U,0U,reset_payload.bytes.size()),2U,11U,"x","x","x"},target.state));
    CHECK(target.state.history().snapshot_count() == 0U);
}
