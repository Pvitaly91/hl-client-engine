#include "delta_test_fixture.hpp"
#include <hlclient/app/entity_follow_presentation.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <memory>

namespace {
namespace c=hlclient::client;
namespace g=hlclient::goldsrc;
namespace d=hlclient::test::delta_fixture;
constexpr d::Field fields[]{
    {"origin[0]",0x80000004U,0,16,32000,4000},
    {"origin[1]",0x80000004U,4,16,32000,4000},
    {"origin[2]",0x80000004U,8,16,32000,4000},
    {"angles[0]",0x10U,12,16},{"angles[1]",0x10U,16,16},{"angles[2]",0x10U,20,16},
    {"modelindex",8,24,16},{"solid",2,28,3},{"movetype",8,32,4}};
void zero_delta(d::BitWriter& writer) {writer.write(0,3);}
g::OwnedServicePayload payload(std::vector<std::byte> bytes,unsigned sequence) {
    auto result=d::owning_payload(std::move(bytes));result.source_sequence=sequence;return result;
}
g::RuntimeReplayRecord record(std::vector<std::byte> bytes,unsigned ordinal) {
    auto owned=payload(std::move(bytes),ordinal+300);auto cursor=g::StockRuntimeSourceCursor::create(0,0,owned.bytes.size());
    REQUIRE(cursor);g::RuntimeReplayRecord result{1,ordinal+9000,ordinal,std::move(owned),*cursor};
    result.received_at=g::SoundTime{}+std::chrono::seconds{100}+std::chrono::milliseconds{ordinal*20};return result;
}
g::RuntimeReplayInitialization initialization(bool complete=true) {
    g::DeltaSchemaRegistryBuilder builder;
    const d::Field health[]{{"health",0x80000004U,0,10}};
    const d::Field clip[]{{"m_iClip",0x80000008U,0,10}};
    std::vector<d::Field> entity_fields{std::begin(fields),std::end(fields)};
    if(!complete)entity_fields.erase(entity_fields.begin()+5); // Missing roll: not a complete pose.
    for(const auto& encoded:std::array{d::schema("entity_state_t",entity_fields),d::schema("clientdata_t",health),d::schema("weapon_data_t",clip)}) {
        const auto parsed=g::DeltaDescriptionParser{}.parse(encoded,0);REQUIRE(parsed);REQUIRE(builder.insert(*parsed.schema));
    }
    auto schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(builder).publish());
    d::BitWriter writer;writer.write(10,11);writer.write(0,2);zero_delta(writer);
    writer.write(0xffff,16);writer.write(0,6);writer.align_zero();auto source=payload(writer.bytes(),99);
    const auto cursor=g::StockRuntimeSourceCursor::create(0,0,source.bytes.size());REQUIRE(cursor);
    const auto decoded=g::GoldSrcEntityBaselineDecoder{}.decode({&source,*cursor,1,1,1,
        "entity_state_t","entity_state_t","entity_state_t",source.bytes.size()*8},*schemas);REQUIRE(decoded);
    g::RuntimeReplayInitialization init;init.generation=1;init.max_clients=1;
    init.schemas=std::move(schemas);init.baselines=std::make_shared<const g::EntityBaselineRegistryState>(std::move(*decoded.registry));
    init.schema_bindings.player_entity="entity_state_t";init.schema_bindings.custom_entity="entity_state_t";return init;
}
std::vector<std::byte> entities(unsigned offset,unsigned model=37,bool complete=true) {
    d::BitWriter writer;writer.write(7,8);writer.write(std::bit_cast<std::uint32_t>(100.F+offset),32);
    writer.write(g::kGoldSrcSvcPacketEntitiesOpcode,8);writer.write(1,16);
    writer.write(0,1);writer.write(0,1);writer.write(10,6);writer.write(0,1);writer.write(0,1);
    if(complete) {writer.write(2,3);writer.write(0x1ff,16);}else {writer.write(1,3);writer.write(0xff,8);}
    for(const unsigned coordinate:{80U+offset,160U+offset,240U+offset})writer.write(coordinate*2,16);
    writer.write(4096U+offset,16);writer.write(8192U+offset,16);if(complete)writer.write(12288U+offset,16);
    writer.write(model,16);writer.write(4,3);writer.write(7,4);writer.write(0,16);writer.align_zero();return writer.bytes();
}
void append_decal(std::vector<std::byte>& bytes,unsigned target=10) {
    bytes.insert(bytes.end(),{std::byte{23},std::byte{118},std::byte{88},std::byte{0},
        std::byte{168},std::byte{0},std::byte{248},std::byte{0},std::byte{255},
        static_cast<std::byte>(target&255),static_cast<std::byte>(target>>8)});
}
c::RuntimeClientObservationState observed(unsigned ordinal,unsigned model=37,bool present=true) {
    c::RuntimeClientObservationState state;state.generation=1;
    state.entity_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{ordinal,ordinal,1,0,8}};
    if(present) {
        c::RuntimePacketEntityObservation entity;entity.entity_number=10;entity.model_index=model;
        entity.ordinary_visual_schema=true;entity.origin={0.,0.,0.};entity.angles={0.,0.,0.};state.packet_entities.push_back(entity);
    }
    return state;
}
}
TEST_CASE("E30 brush decal freezes complete canonical target pose in its exact atomic record",
    "[e30][surface-decal][birth][runtime-replay][transaction][no-stock]") {
    c::ClientWorldState world;auto replay=g::RuntimeReplaySession::initialize(initialization(),world);REQUIRE(replay);
    auto bytes=entities(0);append_decal(bytes);const auto first=replay.session->apply_record(record(bytes,1));REQUIRE(first);
    REQUIRE(first.event->temporary_effects.size()==1);const auto retained=first.event->temporary_effects[0];
    REQUIRE(retained.decal_target_pose);CHECK(retained.decal_target_pose->model_slot==37);
    const auto& canonical=world.runtime_observation()->packet_entities[0];
    CHECK(retained.decal_target_pose->origin.x==*canonical.origin.x);CHECK(retained.decal_target_pose->origin.y==*canonical.origin.y);
    CHECK(retained.decal_target_pose->origin.z==*canonical.origin.z);CHECK(retained.decal_target_pose->angles_degrees.x==*canonical.angles.x);
    CHECK(retained.decal_target_pose->angles_degrees.y==*canonical.angles.y);CHECK(retained.decal_target_pose->angles_degrees.z==*canonical.angles.z);
    auto malformed=entities(800);malformed.push_back(std::byte{255});CHECK_FALSE(replay.session->apply_record(record(malformed,2)));
    CHECK(*world.runtime_observation()->packet_entities[0].origin.x==retained.decal_target_pose->origin.x);
    REQUIRE(replay.session->apply_record(record(entities(800),2)));
    CHECK(*world.runtime_observation()->packet_entities[0].origin.x!=retained.decal_target_pose->origin.x);
    CHECK(*world.runtime_observation()->packet_entities[0].angles.y!=retained.decal_target_pose->angles_degrees.y);
    CHECK(retained.decal_target_pose->origin.x==10.F);CHECK(retained.decal_target_pose->origin.y==20.F);
    CHECK(retained.decal_target_pose->origin.z==30.F);CHECK(retained.origin.x==11.F); // Point is not replaced by entity origin.
    replay.session.reset();bytes.clear();CHECK(retained.decal_target_pose->model_slot==37);CHECK(retained.decal_target_pose->origin.x==10.F);
}
TEST_CASE("E30 world missing target zero model and incomplete schema never manufacture a target pose",
    "[e30][surface-decal][birth][runtime-replay][reject][no-stock]") {
    for(unsigned scenario=0;scenario<4;++scenario) {
        c::ClientWorldState world;auto replay=g::RuntimeReplaySession::initialize(initialization(scenario!=3),world);REQUIRE(replay);
        auto bytes=entities(0,scenario==2 ? 0 : 37,scenario!=3);append_decal(bytes,scenario==0 ? 0 : scenario==1 ? 11 : 10);
        const auto result=replay.session->apply_record(record(bytes,1));REQUIRE(result);REQUIRE(result.event->temporary_effects.size()==1);
        CHECK_FALSE(result.event->temporary_effects[0].decal_target_pose);CHECK(result.event->temporary_effects[0].kind==c::TemporaryEffectKind::decal);
    }
}
TEST_CASE("E30 existing entity lifetime birth floor prevents queued decals attaching a later reused slot",
    "[e30][surface-decal][birth][lifetime][no-stock]") {
    auto owner=std::make_unique<hlclient::app::EntityFollowPresentation>();owner->reset(1);
    owner->observe(observed(10));const auto first=owner->lifetime(10);REQUIRE(first!=0);
    CHECK(owner->lifetime(10,9)==0);CHECK(owner->lifetime(10,10)==first);CHECK(owner->lifetime(10,11)==first);
    auto hidden=observed(11);hidden.packet_entities[0].effects=128;owner->observe(hidden);
    CHECK(owner->lifetime(10)==first);CHECK(owner->lifetime(10,10)==first); // NODRAW is not allocation reuse.
    owner->observe(observed(12,37,false));CHECK(owner->lifetime(10)==0);CHECK(owner->lifetime(10,10)==0);
    owner->observe(observed(13));const auto reused=owner->lifetime(10);REQUIRE(reused!=0);CHECK(reused!=first);
    CHECK(owner->lifetime(10,10)==0);CHECK(owner->lifetime(10,12)==0);CHECK(owner->lifetime(10,13)==reused);
    owner->observe(observed(14,38));const auto different=owner->lifetime(10);CHECK(different!=reused);
    CHECK(owner->lifetime(10,13)==0);CHECK(owner->lifetime(10,14)==different);CHECK(owner->lifetime(10,0)==0);
    owner->observe(observed(15,0));CHECK(owner->lifetime(10)==0);owner->reset(2);CHECK(owner->lifetime(10,15)==0);
}
