#pragma once

#include "delta_test_fixture.hpp"
#include <hlclient/goldsrc/packet_entity_decoder.hpp>

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <memory>

namespace hlclient::test::player_origin_fixture {
namespace g = goldsrc;
namespace f = delta_fixture;
using Position = std::array<double, 3U>;

inline std::shared_ptr<const g::DeltaSchemaRegistryState> schemas(bool client_z = true) {
    const f::Field player[]{
        {"origin[0]",0x80000004U,0U,18U,32000U,4000U},
        {"origin[1]",0x80000004U,4U,18U,32000U,4000U},
        {"origin[2]",0x80000004U,8U,18U,32000U,4000U},
        {"angles[0]",0x10U,12U,16U}, {"angles[1]",0x10U,16U,16U},
        {"angles[2]",0x10U,20U,16U}, {"modelindex",8U,24U,10U},
        {"sequence",8U,28U,8U}};
    const f::Field client[]{
        {"origin[0]",0x80000004U,0U,20U,128000U,4000U},
        {"origin[1]",0x80000004U,4U,20U,128000U,4000U},
        {"origin[2]",0x80000004U,8U,20U,128000U,4000U}};
    const f::Field weapon[]{{"m_iClip",0x80000008U,0U,10U}};
    g::DeltaSchemaRegistryBuilder builder;
    for (const auto& bytes : std::array{f::schema("entity_state_t",player),
            f::schema("entity_state_player_t",player),
            f::schema("clientdata_t",std::span{client}.first(client_z ? 3U : 2U)),
            f::schema("weapon_data_t",weapon)}) {
        const auto parsed=g::DeltaDescriptionParser{}.parse(bytes,0U);
        REQUIRE(parsed);
        REQUIRE(builder.insert(*parsed.schema));
    }
    return std::make_shared<const g::DeltaSchemaRegistryState>(std::move(builder).publish());
}

inline std::shared_ptr<const g::EntityBaselineRegistryState> baselines(const g::DeltaSchemaRegistryState& registry) {
    auto built=g::EntityBaselineRegistryBuilder{registry,{},
        g::EntitySnapshotCompatibilityProfile::public_goldsrc48_entity_delta_v1}.publish();
    REQUIRE(built);
    return std::make_shared<const g::EntityBaselineRegistryState>(std::move(*built.state));
}

inline void coordinate(f::BitWriter& w,double value,std::uint32_t scale,std::size_t bits) {
    const auto magnitude=static_cast<std::uint32_t>(std::llround(std::abs(value)*scale));
    w.write((magnitude<<1U)|(value<0.0 ? 1U : 0U),bits);
}

inline void client_data(f::BitWriter& w,const Position& origin,bool client_z=true) {
    w.write(15U,8U); w.write(0U,1U); // svc_clientdata, no base
    w.write(1U,3U); w.write(client_z ? 7U : 3U,8U);
    for(std::size_t i=0;i<(client_z ? 3U : 2U);++i) coordinate(w,origin[i],32U,20U);
    w.write(0U,1U); w.align_zero(); // no weapon update
}

// Project-owned public wire fixture. For receiver A (entity1), nearby entity2
// uses the preceding local player as a full-packet base. Equal height is omitted;
// at a different height it is explicitly encoded. Receiver B sees entity1 before
// its own local record, reproducing the asymmetric ordering without real assets.
inline std::vector<std::byte> full(std::uint32_t receiver,const Position& local,
    const Position& remote,bool intra=true,bool include_client=true,bool client_z=true,
    bool client_after=false,float time=30.0F) {
    f::BitWriter w;
    w.write(7U,8U); w.write(std::bit_cast<std::uint32_t>(time),32U);
    if(include_client && !client_after) client_data(w,local,client_z);
    w.write(40U,8U); w.write(2U,16U);
    for(std::uint32_t number=1U;number<=2U;++number) {
        w.write(1U,1U); // sequential full-packet entity number
        w.write(0U,1U); // ordinary player schema
        const bool base=number==2U && intra;
        w.write(base ? 1U : 0U,1U);
        if(base) w.write(1U,6U);
        const bool own=number==receiver;
        const bool explicit_z=!base || local[2]!=remote[2];
        const auto mask=own ? 0x40U : (0x43U|(explicit_z ? 4U : 0U));
        w.write(1U,3U); w.write(mask,8U);
        if(!own) {
            coordinate(w,remote[0],8U,18U); coordinate(w,remote[1],8U,18U);
            if(explicit_z) coordinate(w,remote[2],8U,18U);
        }
        w.write(7U,10U); // project-owned Studio resource slot
    }
    w.write(0U,16U); w.align_zero();
    if(include_client && client_after) client_data(w,local,client_z);
    return w.bytes();
}

inline g::OwnedServicePayload payload(std::vector<std::byte> bytes,std::uint32_t sequence) {
    auto result=f::owning_payload(std::move(bytes)); result.source_sequence=sequence; return result;
}

inline double value(const g::EntitySnapshotState& state,std::uint32_t entity,std::string_view name) {
    const auto* item=state.find_exact(entity); REQUIRE(item);
    const auto* field=item->object().find_exact(name); REQUIRE(field);
    REQUIRE(std::holds_alternative<double>(field->value()));
    return std::get<double>(field->value());
}
} // namespace hlclient::test::player_origin_fixture
