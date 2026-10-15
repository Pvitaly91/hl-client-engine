#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>

#include "delta_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

namespace {
namespace c = hlclient::client;
namespace g = hlclient::goldsrc;
namespace f = hlclient::test::delta_fixture;

g::RuntimeReplayInitialization initialization(bool fields = true,
    std::uint32_t second_type = 0x80000004U, std::uint64_t generation = 1U) {
    // Different descriptor order and offsets prove name-based projection, not
    // SDK ABI offsets. Float scale matches the public weapon descriptor /128.
    const f::Field entity[]{{"origin[0]",0x80000004U,0U,22U,512000U,4000U}};
    const f::Field client[]{{"health",0x80000004U,0U,22U,512000U,4000U},
        {"fuser2",0x80000004U,900U,22U,512000U,4000U},
        {"fuser3",0x80000004U,400U,22U,512000U,4000U}};
    const f::Field weapon[]{{"m_iId",8U,1000U,10U},
        {"m_iClip",0x80000008U,1004U,10U},
        {"fuser3",0x80000004U,612U,22U,512000U,4000U},
        {"fuser2",second_type,1408U,22U,512000U,4000U}};
    g::DeltaSchemaRegistryBuilder builder;
    for (const auto& bytes : std::array{f::schema("entity_state_t",entity),
            f::schema("clientdata_t",client),
            f::schema("weapon_data_t",std::span{weapon}.first(fields ? 4U : 2U))}) {
        const auto parsed=g::DeltaDescriptionParser{}.parse(bytes,0U);
        REQUIRE(parsed); REQUIRE(builder.insert(*parsed.schema));
    }
    auto schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(builder).publish());
    auto baseline=g::EntityBaselineRegistryBuilder{*schemas,{},
        g::EntitySnapshotCompatibilityProfile::public_goldsrc48_entity_delta_v1}.publish();
    REQUIRE(baseline);
    g::RuntimeReplayInitialization init;
    init.generation=generation; init.max_clients=2U; init.schemas=schemas;
    init.baselines=std::make_shared<const g::EntityBaselineRegistryState>(std::move(*baseline.state));
    init.schema_bindings.player_entity="entity_state_t";
    init.schema_bindings.custom_entity="entity_state_t";
    // Protocol-only host: no Half-Life module constructed or linked here.
    return init;
}

void scalar(f::BitWriter& writer,double value,std::uint32_t scale,std::size_t bits) {
    const auto magnitude=static_cast<std::uint32_t>(std::llround(std::abs(value)*scale));
    writer.write((magnitude<<1U)|(value<0 ? 1U : 0U),bits);
}
std::vector<std::byte> wire(double second,double third,std::uint8_t mask=15U,
    std::optional<std::uint8_t> base={},bool slot=true) {
    f::BitWriter writer;
    writer.write(7U,8U);writer.write(std::bit_cast<std::uint32_t>(20.0F),32U);
    writer.write(15U,8U);writer.write(base ? 1U : 0U,1U);
    if(base)writer.write(*base,8U);
    writer.write(base ? 0U : 1U,3U);
    if(!base) {
        writer.write(7U,8U);scalar(writer,100,128U,22U);
        scalar(writer,.25,128U,22U);scalar(writer,.5,128U,22U);
    }
    writer.write(slot ? 1U : 0U,1U);
    if(slot) {
        writer.write(7U,6U); // Session wire slot is NOT the semantic weapon ID.
        writer.write(mask ? 1U : 0U,3U);if(mask)writer.write(mask,8U);
        if(mask&1U)writer.write(12U,10U);
        if(mask&2U)scalar(writer,-1,1U,10U);
        if(mask&4U)scalar(writer,third,128U,22U);
        if(mask&8U)scalar(writer,second,128U,22U);
    }
    writer.write(0U,1U);writer.align_zero();return writer.bytes();
}
g::RuntimeReplayRecord record(std::vector<std::byte> bytes,std::uint32_t sequence=20U,
    std::size_t ordinal=1U,std::uint64_t generation=1U) {
    auto payload=f::owning_payload(std::move(bytes));payload.source_sequence=sequence;
    payload.source_acknowledgement=sequence-1U;
    const auto cursor=g::StockRuntimeSourceCursor::create(0,0,payload.bytes.size());REQUIRE(cursor);
    return {generation,1000U+ordinal,ordinal,std::move(payload),*cursor};
}
const c::RuntimeWeaponSlotObservation& slot(const c::ClientWorldState& world) {
    REQUIRE(world.runtime_observation());REQUIRE(world.runtime_observation()->weapon_slots.size()==64U);
    return world.runtime_observation()->weapon_slots[7U];
}
}

TEST_CASE("E24 neutral weapon auxiliary floats retain exact owning negotiated values",
    "[e24][no-stock][clientdata][runtime-replay][game-api]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
    auto incoming=record(wire(10.125,-1));REQUIRE(made.session->apply_record(incoming));
    const auto observed=world.runtime_observation();
    CHECK(slot(world).wire_slot==7U);CHECK(slot(world).weapon_id==12U);CHECK(slot(world).clip==-1);
    CHECK(slot(world).auxiliary_float2==10.125);CHECK(slot(world).auxiliary_float3==-1.0);
    CHECK(slot(world).auxiliary_float2_updated_in_record);CHECK(slot(world).auxiliary_float3_updated_in_record);
    CHECK(observed->receiving_client->auxiliary_float2==.25);
    CHECK(observed->receiving_client->auxiliary_float3==.5);
    REQUIRE(observed->client_metadata.source);
    CHECK(observed->client_metadata.source->record_identity==1001U);
    CHECK(observed->client_metadata.source->source_transport_sequence==20U);
    CHECK(observed->client_metadata.source->carrier_acknowledgement==19U);
    incoming.payload.bytes.clear();made.session->finish();made.session.reset();
    CHECK(observed->weapon_slots[7].auxiliary_float2==10.125);
    CHECK(c::valid_runtime_observation(*observed));
}

TEST_CASE("E24 weapon auxiliary freshness distinguishes exact mask from unrelated delta and retention",
    "[e24][no-stock][clientdata][runtime-replay][game-api]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(wire(0,-1))));
    // An unrelated clip record cannot make a retained zero a new clear/cancel.
    REQUIRE(made.session->apply_record(record(wire(0,0,2U,20U),21U,2U)));
    CHECK(slot(world).auxiliary_float2==0);CHECK(slot(world).auxiliary_float3==-1);
    CHECK_FALSE(slot(world).auxiliary_float2_updated_in_record);
    CHECK_FALSE(slot(world).auxiliary_float3_updated_in_record);
    CHECK(world.runtime_observation()->client_metadata.freshness==c::RuntimeObservationFreshness::observed_in_record);
    auto frame=made.session->decoder_state().client_data_state().current_frame();REQUIRE(frame);
    CHECK(frame->find_weapon_slot(7)->wire_field_updated("m_iClip"));
    CHECK_FALSE(frame->find_weapon_slot(7)->wire_field_updated("fuser2"));
    // Explicit equal zero IS new evidence, although the value object is shared.
    REQUIRE(made.session->apply_record(record(wire(0,0,8U,21U),22U,3U)));
    CHECK(slot(world).auxiliary_float2_updated_in_record);
    CHECK_FALSE(slot(world).auxiliary_float3_updated_in_record);
    const auto repeated=made.session->decoder_state().client_data_state().current_frame();
    CHECK(repeated->find_weapon_slot(7)->shares_object_with(*frame->find_weapon_slot(7)));
    CHECK(repeated->find_weapon_slot(7)->wire_field_updated("fuser2"));
    // A frame omitting the whole slot has reconstructed values, not fresh fields.
    REQUIRE(made.session->apply_record(record(wire(0,0,0U,22U,false),23U,4U)));
    CHECK_FALSE(slot(world).auxiliary_float2_updated_in_record);
    CHECK_FALSE(slot(world).auxiliary_float3_updated_in_record);
    const auto source=world.runtime_observation()->client_metadata.source;
    REQUIRE(made.session->apply_record(record({std::byte{1}},24U,5U)));
    CHECK(world.runtime_observation()->client_metadata.source==source);
    CHECK(world.runtime_observation()->client_metadata.freshness==c::RuntimeObservationFreshness::retained);
    CHECK_FALSE(slot(world).auxiliary_float2_updated_in_record);
    // No-base selection resets descriptor-backed values, even when mask omits them.
    REQUIRE(made.session->apply_record(record(wire(0,0,3U),25U,6U)));
    CHECK(slot(world).auxiliary_float2==0);CHECK(slot(world).auxiliary_float3==0);
    CHECK(slot(world).auxiliary_float2_updated_in_record);CHECK(slot(world).auxiliary_float3_updated_in_record);
}

TEST_CASE("E24 weapon auxiliary delta inherits independently and malformed suffix publishes nothing",
    "[e24][no-stock][clientdata][runtime-replay][game-api]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(wire(10.125,0))));
    REQUIRE(made.session->apply_record(record(wire(0,10.5,4U,20U),21U,2U)));
    CHECK(slot(world).auxiliary_float2==10.125);CHECK(slot(world).auxiliary_float3==10.5);
    CHECK_FALSE(slot(world).auxiliary_float2_updated_in_record);CHECK(slot(world).auxiliary_float3_updated_in_record);
    const auto before=world.runtime_observation();auto invalid=wire(0,-1,12U,21U);invalid.push_back(std::byte{254});
    CHECK_FALSE(made.session->apply_record(record(invalid,22U,3U)));
    CHECK(world.runtime_observation()==before);
    auto valid=record(wire(0,-1,12U,21U),22U,3U);REQUIRE(made.session->apply_record(valid));
    CHECK(slot(world).auxiliary_float2==0);CHECK(slot(world).auxiliary_float3==-1);
    const auto after=world.runtime_observation();CHECK_FALSE(made.session->apply_record(valid));
    CHECK(world.runtime_observation()==after);
    REQUIRE_FALSE(made.session->reset_generation(initialization(true,0x80000004U,2U)));
    CHECK(world.runtime_observation()->weapon_slots.empty());
    CHECK_FALSE(made.session->apply_record(valid));
    CHECK(after->weapon_slots[7].auxiliary_float3==-1);
}

TEST_CASE("E24 absent weapon fusers remain unavailable and wrong negotiated type is rejected atomically",
    "[e24][no-stock][clientdata][runtime-replay][game-api]") {
    for(const auto type:{0x80000008U,0x00000004U}) {
        CAPTURE(type);c::ClientWorldState world;
        auto made=g::RuntimeReplaySession::initialize(initialization(true,type),world);REQUIRE(made);
        const auto before=world.runtime_observation();
        const auto rejected=made.session->apply_record(record(wire(1,0)));
        REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
        CHECK(rejected.error->code==g::RuntimeReplayErrorCode::semantic_schema_mismatch);
        CHECK(world.runtime_observation()==before);
    }
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(false),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(wire(0,0,3U))));
    CHECK_FALSE(slot(world).auxiliary_float2);CHECK_FALSE(slot(world).auxiliary_float3);
    CHECK_FALSE(slot(world).auxiliary_float2_updated_in_record);CHECK_FALSE(slot(world).auxiliary_float3_updated_in_record);
}

TEST_CASE("E24 neutral weapon fuser validation preserves established canonical hash contract",
    "[e24][no-stock][clientdata][runtime-replay][game-api]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(wire(10.125,-1))));
    const auto original=*world.runtime_observation();auto changed=original;
    changed.weapon_slots[7].auxiliary_float2=999;
    changed.weapon_slots[7].auxiliary_float3.reset();changed.weapon_slots[7].auxiliary_float3_updated_in_record=false;
    CHECK(changed!=original);CHECK(c::valid_runtime_observation(changed));
    CHECK(c::runtime_observation_canonical_hash(changed)==c::runtime_observation_canonical_hash(original));
    for(const double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()}) {
        auto invalid=original;invalid.weapon_slots[7].auxiliary_float2=bad;
        CHECK_FALSE(c::valid_runtime_observation(invalid));
        invalid=original;invalid.weapon_slots[7].auxiliary_float3=bad;
        CHECK_FALSE(c::valid_runtime_observation(invalid));
    }
    changed=original;changed.weapon_slots[7].auxiliary_float2.reset();CHECK_FALSE(c::valid_runtime_observation(changed));
    changed=original;changed.client_metadata.freshness=c::RuntimeObservationFreshness::retained;
    CHECK_FALSE(c::valid_runtime_observation(changed));
}
