#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include "delta_test_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <bit>
#include <cmath>
namespace {
namespace c=hlclient::client;namespace g=hlclient::goldsrc;namespace d=hlclient::test::delta_fixture;
g::RuntimeReplayInitialization bounds_initialization(bool include=true,bool valid_type=true) {
    std::vector<d::Field> fields{{"owner",8U,0U,5U},{"solid",2U,4U,8U},{"modelindex",8U,8U,10U},
      {"origin[0]",0x80000004U,12U,16U,32000U},{"origin[1]",0x80000004U,16U,16U,32000U},
      {"origin[2]",0x80000004U,20U,16U,32000U}};
    if(include) for(const auto name:{"mins[0]","mins[1]","mins[2]","maxs[0]","maxs[1]","maxs[2]"})
        fields.push_back({name,valid_type ? 0x80000004U : 4U,static_cast<std::uint16_t>(24+fields.size()*4),16U,32000U});
    g::DeltaSchemaRegistryBuilder builder;
    const auto parsed=g::DeltaDescriptionParser{}.parse(d::schema("entity_state_t",fields),0);REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
    auto schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(builder).publish());
    auto baseline=g::EntityBaselineRegistryBuilder{*schemas,{},g::EntitySnapshotCompatibilityProfile::public_goldsrc48_entity_delta_v1}.publish();REQUIRE(baseline);
    g::RuntimeReplayInitialization init;init.generation=1;init.max_clients=2;init.schemas=schemas;
    init.baselines=std::make_shared<const g::EntityBaselineRegistryState>(std::move(*baseline.state));
    init.schema_bindings.player_entity="entity_state_t";init.schema_bindings.custom_entity="entity_state_t";return init;
}
std::vector<std::byte> bounds_wire(bool include=true) {
    d::BitWriter w;w.write(7,8);w.write(std::bit_cast<std::uint32_t>(20.F),32);
    w.write(40,8);w.write(1,16);w.write(1,1);w.write(0,1);w.write(0,1);
    w.write(include ? 2U : 1U,3);w.write(include ? 255U : 63U,8);if(include)w.write(15U,8);
    w.write(2,5);w.write(2,8);w.write(237,10);
    for(const int value:{40,0,4})w.write(static_cast<unsigned>(std::abs(value)*8)*2U,16);
    if(include)for(const int value:{-4,-4,-4,4,4,4})w.write(static_cast<unsigned>(std::abs(value)*8)*2U+(value<0 ? 1U : 0U),16);
    w.write(0,16);w.align_zero();return w.bytes();
}
g::RuntimeReplayInitialization initialization(bool value=true,std::uint32_t type=0x80000008U) {
    const d::Field entity[]{{"owner",8U,700U,5U},{"origin[0]",0x80000004U,4U,22U,512000U,4000U}};
    const d::Field client[]{{"health",0x80000004U,0U,22U,512000U,4000U},{"iuser1",0x80000008U,1200U,10U}};
    const d::Field weapon[]{{"m_iId",8U,888U,10U},{"m_iClip",0x80000008U,212U,10U},{"iuser1",type,12U,10U}};
    g::DeltaSchemaRegistryBuilder builder;
    for(const auto& bytes:std::array{d::schema("entity_state_t",entity),d::schema("clientdata_t",client),
        d::schema("weapon_data_t",std::span{weapon}.first(value ? 3U : 2U))}) {
        const auto parsed=g::DeltaDescriptionParser{}.parse(bytes,0);REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
    }
    auto schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(builder).publish());
    auto baseline=g::EntityBaselineRegistryBuilder{*schemas,{},g::EntitySnapshotCompatibilityProfile::public_goldsrc48_entity_delta_v1}.publish();REQUIRE(baseline);
    g::RuntimeReplayInitialization init;init.generation=1;init.max_clients=2;init.schemas=schemas;
    init.baselines=std::make_shared<const g::EntityBaselineRegistryState>(std::move(*baseline.state));
    init.schema_bindings.player_entity="entity_state_t";init.schema_bindings.custom_entity="entity_state_t";
    return init;
}
std::vector<std::byte> wire(int phase=0,unsigned mask=7,std::optional<unsigned> base={},bool slot=true,bool entity=false) {
    d::BitWriter w;w.write(7,8);w.write(std::bit_cast<std::uint32_t>(20.F),32);
    w.write(15,8);w.write(base ? 1U : 0U,1);if(base)w.write(*base,8);
    w.write(base ? 0U : 1U,3);if(!base){w.write(3,8);w.write(25600,22);w.write(14,10);} // unrelated client iuser1=7
    w.write(slot ? 1U : 0U,1);if(slot) {
        w.write(7,6);w.write(mask ? 1U : 0U,3);if(mask)w.write(mask,8);
        if(mask&1U)w.write(14,10);if(mask&2U)w.write(3,10);
        if(mask&4U)w.write((static_cast<unsigned>(std::abs(phase))<<1U)|(phase<0 ? 1U : 0U),10);
    }
    w.write(0,1);w.align_zero();
    if(entity) {w.write(40,8);w.write(1,16);w.write(1,1);w.write(0,1);w.write(0,1);
        w.write(1,3);w.write(1,8);w.write(2,5);w.write(0,16);w.align_zero();}
    return w.bytes();
}
g::RuntimeReplayRecord record(std::vector<std::byte> bytes,unsigned sequence=20,std::size_t ordinal=1) {
    auto payload=d::owning_payload(std::move(bytes));payload.source_sequence=sequence;
    payload.source_acknowledgement=sequence-1;const auto cursor=g::StockRuntimeSourceCursor::create(0,0,payload.bytes.size());REQUIRE(cursor);
    return {1,1000+ordinal,ordinal,std::move(payload),*cursor};
}
const c::RuntimeWeaponSlotObservation& slot(const c::ClientWorldState& world) {return world.runtime_observation()->weapon_slots.at(7);}
}
TEST_CASE("E25 server BBOX bounds are owning optional schema values and publish atomically",
          "[e25][server-box][packet-entities][transactional][no-stock]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(bounds_initialization(),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(bounds_wire())));
    const auto saved=world.runtime_observation();REQUIRE(saved->packet_entities.size()==1);
    const auto& entity=saved->packet_entities.front();CHECK(entity.origin.x==40);CHECK(entity.origin.z==4);
    CHECK(entity.collision_mins.x==-4);CHECK(entity.collision_mins.y==-4);CHECK(entity.collision_mins.z==-4);
    CHECK(entity.collision_maxs.x==4);CHECK(entity.collision_maxs.y==4);CHECK(entity.collision_maxs.z==4);
    CHECK(entity.solid==2U);CHECK(entity.owner_entity_reference==2U);
    auto hash_control=*saved;hash_control.packet_entities[0].collision_mins={};hash_control.packet_entities[0].collision_maxs={};
    CHECK(c::runtime_observation_canonical_hash(hash_control)==saved->canonical_state_hash);
    auto bad=bounds_wire();bad.push_back(std::byte{254});CHECK_FALSE(made.session->apply_record(record(bad,21,2)));
    CHECK(world.runtime_observation()==saved);CHECK_FALSE(made.session->apply_record(record(bounds_wire())));
    CHECK(saved->packet_entities.front().collision_mins.x==-4); // independent of freed/reused input buffers
    c::ClientWorldState absent;auto no_bounds=g::RuntimeReplaySession::initialize(bounds_initialization(false),absent);REQUIRE(no_bounds);
    REQUIRE(no_bounds.session->apply_record(record(bounds_wire(false))));CHECK_FALSE(absent.runtime_observation()->packet_entities.front().collision_mins.complete());
    c::ClientWorldState wrong;auto bad_schema=g::RuntimeReplaySession::initialize(bounds_initialization(true,false),wrong);REQUIRE(bad_schema);
    const auto before=wrong.runtime_observation();CHECK_FALSE(bad_schema.session->apply_record(record(bounds_wire())));CHECK(wrong.runtime_observation()==before);
}
TEST_CASE("E25 neutral signed iuser1 projection is owning exact and separate from clientdata","[e25][clientdata][game-api][no-stock]") {
    for(const int value:{0,1,2,-511,511}) {
        c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
        auto incoming=record(wire(value));REQUIRE(made.session->apply_record(incoming));
        CHECK(slot(world).wire_slot==7);CHECK(slot(world).weapon_id==14);CHECK(slot(world).clip==-1);
        CHECK(slot(world).auxiliary_integer1==value);CHECK(slot(world).auxiliary_integer1_updated_in_record);
        const auto observed=world.runtime_observation();incoming.payload.bytes.clear();made.session->finish();made.session.reset();
        CHECK(observed->weapon_slots[7].auxiliary_integer1==value);CHECK(c::valid_runtime_observation(*observed));
    }
}
TEST_CASE("E25 iuser1 exact presence excludes unrelated fields slots and retained records","[e25][clientdata][freshness][no-stock]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(wire(1))));
    REQUIRE(made.session->apply_record(record(wire(0,2,20),21,2)));
    CHECK(slot(world).auxiliary_integer1==1);CHECK_FALSE(slot(world).auxiliary_integer1_updated_in_record);
    REQUIRE(made.session->apply_record(record(wire(1,4,21),22,3)));CHECK(slot(world).auxiliary_integer1_updated_in_record);
    REQUIRE(made.session->apply_record(record(wire(0,0,22,false),23,4)));CHECK_FALSE(slot(world).auxiliary_integer1_updated_in_record);
    REQUIRE(made.session->apply_record(record({std::byte{1}},24,5)));CHECK_FALSE(slot(world).auxiliary_integer1_updated_in_record);
    CHECK(world.runtime_observation()->client_metadata.freshness==c::RuntimeObservationFreshness::retained);
    REQUIRE(made.session->apply_record(record(wire(0,3),25,6)));CHECK(slot(world).auxiliary_integer1==0);CHECK(slot(world).auxiliary_integer1_updated_in_record);
}
TEST_CASE("E25 iuser1 malformed suffix duplicates and generation reset preserve atomic state","[e25][clientdata][transactional][no-stock]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(wire(1))));const auto before=world.runtime_observation();
    auto bad=wire(2,4,20);bad.push_back(std::byte{254});CHECK_FALSE(made.session->apply_record(record(bad,21,2)));
    CHECK(world.runtime_observation()==before);auto next=record(wire(2,4,20),21,2);REQUIRE(made.session->apply_record(next));
    const auto published=world.runtime_observation();CHECK_FALSE(made.session->apply_record(next));CHECK(world.runtime_observation()==published);
    auto reset=initialization();reset.generation=2;CHECK_FALSE(made.session->reset_generation(reset));
    CHECK(world.runtime_observation()->weapon_slots.empty());CHECK(published->weapon_slots[7].auxiliary_integer1==2);
}
TEST_CASE("E25 absent or wrong iuser1 type stays unavailable or rejects the record","[e25][clientdata][no-stock]") {
    c::ClientWorldState world;auto missing=g::RuntimeReplaySession::initialize(initialization(false),world);REQUIRE(missing);
    REQUIRE(missing.session->apply_record(record(wire(0,3))));CHECK_FALSE(slot(world).auxiliary_integer1);CHECK_FALSE(slot(world).auxiliary_integer1_updated_in_record);
    for(const unsigned type:{8U,0x80000004U}) {
        c::ClientWorldState other;auto made=g::RuntimeReplaySession::initialize(initialization(true,type),other);REQUIRE(made);
        const auto before=other.runtime_observation();CHECK_FALSE(made.session->apply_record(record(wire(1))));CHECK(other.runtime_observation()==before);
    }
}
TEST_CASE("E25 negotiated ordinary owner is optional diagnostic data and excluded from legacy hash","[e25][packet-entities][game-api][no-stock]") {
    c::ClientWorldState world;auto made=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(made);
    REQUIRE(made.session->apply_record(record(wire(1,7,{},true,true))));
    const auto original=*world.runtime_observation();REQUIRE(original.packet_entities.size()==1);
    CHECK(original.packet_entities[0].owner_entity_reference==2);
    auto modified=original;modified.weapon_slots[7].auxiliary_integer1=2;modified.packet_entities[0].owner_entity_reference=1;
    CHECK(c::runtime_observation_canonical_hash(modified)==original.canonical_state_hash);CHECK(c::valid_runtime_observation(modified));
    modified.weapon_slots[7].auxiliary_integer1.reset();CHECK_FALSE(c::valid_runtime_observation(modified));
    modified=original;modified.client_metadata.freshness=c::RuntimeObservationFreshness::retained;CHECK_FALSE(c::valid_runtime_observation(modified));
}
