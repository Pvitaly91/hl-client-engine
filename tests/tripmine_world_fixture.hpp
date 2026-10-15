#pragma once
#include "tripmine_test_fixture.hpp"
#include "hand_grenade_world_test_fixture.hpp"
#include <iostream>
#include "python_357_pmodel_fixture.hpp"
namespace hlclient::tests::tripmine_world_fixture {
namespace c=client;namespace api=game_api;namespace r=renderer;namespace wf=hand_grenade_world_fixture;
inline std::vector<std::byte> owned_mine_model() {
    auto b=literal_minimal_goldsrc_studio_v10();
    const std::vector<std::byte> sequence{b.begin()+kSyntheticStudioSequenceOffset,b.begin()+kSyntheticStudioSequenceGroupOffset};
    const auto sequence_offset=b.size();
    for(unsigned i=0;i<9;++i) {b.insert(b.end(),sequence.begin(),sequence.end());
        studio_write_fixed_string(b,sequence_offset+i*sequence.size(),32,i==7 ? "world" : i==8 ? "ground" : "owned_role");}
    studio_write_i32le(b,164,9);studio_write_i32le(b,168,static_cast<std::int32_t>(sequence_offset));
    const std::vector<std::byte> animation{b.begin()+kSyntheticStudioAnimationOffset,b.begin()+kSyntheticStudioSequenceOffset};
    for(unsigned i=0;i<9;++i) {const auto offset=b.size();b.insert(b.end(),animation.begin(),animation.end());
        studio_write_i32le(b,sequence_offset+i*sequence.size()+124,static_cast<std::int32_t>(offset));}
    constexpr unsigned model_size=112;
    const std::vector<std::byte> model{b.begin()+kSyntheticStudioSubmodelOffset,b.begin()+kSyntheticStudioSubmodelOffset+model_size};
    const auto model_offset=b.size();for(unsigned i=0;i<4;++i)b.insert(b.end(),model.begin(),model.end());
    const std::vector<std::byte> geometry{b.begin()+kSyntheticStudioMeshOffset,b.begin()+kSyntheticStudioTextureOffset};
    for(unsigned i=0;i<4;++i) {
        const auto offset=b.size();b.insert(b.end(),geometry.begin(),geometry.end());
        const auto header=model_offset+i*model_size;
        for(const auto pair:std::array<std::pair<unsigned,std::size_t>,5>{{{76,kSyntheticStudioMeshOffset},
            {84,kSyntheticStudioVertexBonesOffset},{88,kSyntheticStudioVerticesOffset},
            {96,kSyntheticStudioNormalBonesOffset},{100,kSyntheticStudioNormalsOffset}}})
            studio_write_i32le(b,header+pair.first,static_cast<std::int32_t>(offset+pair.second-kSyntheticStudioMeshOffset));
        studio_write_i32le(b,offset+4,static_cast<std::int32_t>(offset+kSyntheticStudioCommandsOffset-kSyntheticStudioMeshOffset));
        studio_write_fixed_string(b,header,64,"owned_body"+std::to_string(i));
    }
    studio_write_i32le(b,kSyntheticStudioBodyPartOffset+64,4);
    studio_write_i32le(b,kSyntheticStudioBodyPartOffset+72,static_cast<std::int32_t>(model_offset));
    studio_write_i32le(b,72,static_cast<std::int32_t>(b.size()));return b;
}
struct Owned {
    ScopedLocalResourceTestRoot root;std::unique_ptr<app::RuntimeReplayLocalAssets> assets;
    c::ClientWorldState world;tripmine_fixture::Module module;
    c::RuntimeClientObservationState state;
    Owned() {
        (void)wf::owned(root);
        SyntheticBspBuilder bsp;const std::string entities="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
        const auto bytes=std::as_bytes(std::span{entities});bsp.lump(SyntheticBspLumpId::entities).assign(bytes.begin(),bytes.end());
        auto& nodes=bsp.lump(SyntheticBspLumpId::nodes);
        nodes[4]=std::byte{0xfe};nodes[5]=std::byte{0xff};nodes[6]=nodes[7]=std::byte{0xff}; // above-floor air
        root.write("valve","maps/test.bsp",bsp.build());
        root.write("valve","models/v_tripmine.mdl",owned_mine_model());
        root.write("valve","models/p_tripmine.mdl",python_fixture::remote_model(true));
        root.write("valve","models/player.mdl",python_fixture::remote_model(false));
        root.write("valve","sprites/laserbeam.spr",sprite_fixture::literal_single_sprite(1));
        const auto resources=readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0},
            {2,"models/v_tripmine.mdl",75,0,0},{2,"models/p_tripmine.mdl",71,0,0},{2,"models/player.mdl",7,0,0},
            {2,"models/grenade.mdl",312,0,0},{2,"sprites/laserbeam.spr",315,0,0},
            {2,"sprites/owned_explosion.spr",319,0,0},{2,"sprites/owned_smoke.spr",320,0,0}});
        auto made=app::RuntimeReplayLocalAssets::create(resources,readiness_fixture::parse_server_info("maps/test.bsp"),
            root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
        REQUIRE(made.projection);assets=std::move(made.projection);state=tripmine_fixture::state();
        state.packet_entities={mine()};wf::stamp(state,1,1);REQUIRE(assets->reset_generation(state,world));
        world.set_camera({{0,0,40},{0,0,16},{0,1,0},1.047F,.1F,256});
    }
    static c::RuntimePacketEntityObservation mine(unsigned entity=40) {
        c::RuntimePacketEntityObservation e;e.entity_number=entity;e.model_index=75;e.origin={0.,0.,16.};e.angles={0.,0.,0.};
        e.body=3;e.sequence=7;e.frame=0;e.frame_rate=0;e.animation_time_seconds=1;e.ordinary_visual_schema=true;
        e.brush_move_type=5;e.solid=0;e.effects=0;e.owner_entity_reference=1;
        e.render_effect=0;e.render_amount=255;e.render_mode=0;e.skin=0;
        e.collision_mins={-8.,-8.,-8.};e.collision_maxs={8.,8.,8.};return e;
    }
    static c::RuntimePacketEntityObservation beam(unsigned entity=50,unsigned owner=40) {
        c::RuntimePacketEntityObservation e;e.entity_number=entity;e.origin={-12.,0.,16.};e.custom_beam.emplace();
        auto& b=*e.custom_beam;b.type=c::RuntimeCustomBeamObservation::Type::entity_point;b.end_reference={owner,0};
        // Pinned custom delta POST multiplies server width10/scroll255 by .1.
        b.sprite_model_index=315;b.width_units=1;b.color={0,214,198};b.opacity=64;b.scroll_rate=25.5;
        b.complete=b.supported=true;return e;
    }
    void publish(unsigned record,double server,double app=10) {
        wf::stamp(state,record,server);wf::project(*assets,state,world);
        REQUIRE(assets->present_entities(module.host,app,1,world,{320,240}));
        if(world.entity_frame()->studio_instances().empty()) {
            for(const auto& [status,count]:assets->summary().coverage)
                std::cout<<"e26-fixture-coverage status="<<static_cast<int>(status)<<" count="<<count<<'\n';
            const auto data=assets->studio_model_data(75);
            if(data)std::cout<<"e26-fixture-model sequences="<<data->sequences.size()<<" models="<<data->submodels.size()<<'\n';
        }
    }
    r::RenderScene scene(double at=10) {
        r::RenderScene out;out.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
        assets->present_entity_effects(out,at);return out;
    }
};
}
