#pragma once
#include "hand_grenade_world_test_fixture.hpp"
#include "python_357_pmodel_fixture.hpp"
#include "effect_palette_test_fixture.hpp"
#include <hlclient/goldsrc/runtime_replay_session.hpp>

namespace hlclient::tests::snark_world_fixture {
namespace c=client;namespace g=goldsrc;namespace api=game_api;namespace r=renderer;
namespace wf=hand_grenade_world_fixture;

// Independently authored geometry/animation. Model names exercise resource
// selection only; no installed model, server AI, fuse or damage is copied.
inline std::vector<std::byte> monster_model() {
    auto b=literal_minimal_goldsrc_studio_v10();
    const std::vector<std::byte> sequence{b.begin()+kSyntheticStudioSequenceOffset,b.begin()+kSyntheticStudioSequenceGroupOffset};
    const auto sequences=b.size();
    for(unsigned i=0;i<4;++i) {
        b.insert(b.end(),sequence.begin(),sequence.end());
        studio_write_fixed_string(b,sequences+i*sequence.size(),32,i==3 ? "owned_run" : "owned_idle");
    }
    studio_write_i32le(b,164,4);studio_write_i32le(b,168,static_cast<std::int32_t>(sequences));
    const std::vector<std::byte> animation{b.begin()+kSyntheticStudioAnimationOffset,b.begin()+kSyntheticStudioSequenceOffset};
    for(unsigned i=0;i<4;++i) {
        const auto at=b.size();b.insert(b.end(),animation.begin(),animation.end());
        studio_write_i32le(b,sequences+i*sequence.size()+124,static_cast<std::int32_t>(at));
    }
    studio_write_i32le(b,72,static_cast<std::int32_t>(b.size()));return b;
}
inline std::vector<std::byte> blood_sprite() {
    // Like the approved ALPHA_TEST blood resources, covered pixels are gray;
    // only transparent index255 padding is blue. Ordinary tint must preserve
    // source intensity and mask coverage, not brighten blue filtered edges.
    auto b=sprite_fixture::literal_header_and_palette(6,3,2,0,8,8);
    b[sprite_fixture::kPaletteOffset+3]=std::byte{255};
    b[sprite_fixture::kPaletteOffset+4]=std::byte{255};
    b[sprite_fixture::kPaletteOffset+5]=std::byte{255};
    b[sprite_fixture::kPaletteOffset+255*3]=std::byte{0};
    b[sprite_fixture::kPaletteOffset+255*3+1]=std::byte{0};
    b[sprite_fixture::kPaletteOffset+255*3+2]=std::byte{255};
    for(unsigned i=0;i<6;++i) {
        sprite_fixture::FrameSpec frame{-4,4,8,8,std::vector<std::byte>(64,std::byte{1})};
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            if(x==0 || y==0 || x==7 || y==7)frame.pixels[y*8+x]=std::byte{255};
        sprite_fixture::append_single_entry(b,frame);
    }
    return b;
}
inline c::CommittedTemporaryEffect blood(unsigned record=1,unsigned palette=195,unsigned scale=16) {
    c::CommittedTemporaryEffect e;e.generation=1;e.record=e.record_ordinal=record;
    e.kind=c::TemporaryEffectKind::blood_sprite;e.origin={0,0,16};
    e.blood_sprite_indices={321,322};e.palette_color=static_cast<std::uint8_t>(palette);
    e.blood_scale=static_cast<std::uint8_t>(scale);return e;
}
inline std::vector<std::byte> blood_wire(std::int16_t spray=321,std::int16_t drops=322,
    unsigned palette=195,unsigned scale=16) {
    std::vector<std::byte> b{std::byte{23},std::byte{115}};
    for(const std::int16_t v:{std::int16_t{0},std::int16_t{0},std::int16_t{128},spray,drops})
        sprite_fixture::append_u16_le(b,std::bit_cast<std::uint16_t>(v));
    b.push_back(static_cast<std::byte>(palette));b.push_back(static_cast<std::byte>(scale));return b;
}
inline c::RuntimePacketEntityObservation monster(unsigned number=40,unsigned model=237) {
    c::RuntimePacketEntityObservation e;e.entity_number=number;e.model_index=model;
    e.origin={0.,0.,16.};e.angles={0.,0.,0.};e.ordinary_visual_schema=true;
    e.brush_move_type=10;e.solid=2;e.sequence=3;e.body=e.skin=0;e.frame=0;
    e.animation_time_seconds=1;e.frame_rate=1;e.effects=0;e.render_mode=e.render_effect=0;e.render_amount=255;
    e.owner_entity_reference=1;e.controllers.fill(0);e.blending.fill(0);
    e.collision_mins={-4.,-4.,0.};e.collision_maxs={4.,4.,8.};return e;
}
inline c::RuntimePacketEntityObservation player() {
    auto p=monster(2,7);p.weapon_model_index=71;p.player_movement_schema=true;
    p.brush_move_type=3;p.sequence=p.gait_sequence=0;p.solid=3;p.use_hull=0;
    p.angles={0.,90.,0.};p.controllers.fill(127);p.blending.fill(127);return p;
}
struct Owned {
    ScopedLocalResourceTestRoot root;std::unique_ptr<app::RuntimeReplayLocalAssets> assets;
    c::ClientWorldState world;api::GameClientHost host{games::halflife::make_half_life_client_module()};
    c::RuntimeClientObservationState state;unsigned slot;
    explicit Owned(unsigned model=237,bool available=true,bool spray_available=true,bool drop_available=true):slot{model} {
        (void)wf::owned(root);
        if(available)root.write("valve","models/w_squeak.mdl",monster_model());
        auto nest=monster_model();studio_write_fixed_string(nest,kSyntheticStudioSequenceOffset,32,"owned_pickup");
        // The relocated sequence descriptors, not the old unused descriptor,
        // are part of this model's independently selected identity.
        const auto sequence_offset=literal_minimal_goldsrc_studio_v10().size();
        studio_write_fixed_string(nest,sequence_offset+176,32,"owned_pickup");
        root.write("valve","models/w_sqknest.mdl",nest);
        root.write("valve","models/p_squeak.mdl",python_fixture::remote_model(true));
        root.write("valve","models/player.mdl",python_fixture::remote_model(false));
        if(spray_available)root.write("valve","sprites/bloodspray.spr",blood_sprite());
        if(drop_available)root.write("valve","sprites/blood.spr",blood_sprite());
        root.write("valve","gfx/palette.lmp",effect_palette_fixture::bytes());
        const auto resources=readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0},
            {2,"models/w_squeak.mdl",static_cast<std::uint16_t>(slot),0,0},{2,"models/w_sqknest.mdl",312,0,0},
            {2,"models/p_squeak.mdl",71,0,0},{2,"models/player.mdl",7,0,0},
            {2,"sprites/bloodspray.spr",321,0,0},{2,"sprites/blood.spr",322,0,0}});
        auto made=app::RuntimeReplayLocalAssets::create(resources,readiness_fixture::parse_server_info("maps/test.bsp"),
            root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client,"gfx/palette.lmp");
        REQUIRE(made.projection);assets=std::move(made.projection);state=wf::state(slot);state.packet_entities={monster(40,slot)};
        wf::stamp(state,1,1);REQUIRE(assets->reset_generation(state,world));host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        REQUIRE(assets->effect_palette().status==api::EffectPaletteStatus::ready);
        bind_effects();
    }
    void bind_effects() {
        host.bind_effect_palette(assets->effect_palette());
        host.bind_effect_resource({api::EffectResourceBinding::Kind::model,321,"sprites/bloodspray.spr"});
        host.bind_effect_resource({api::EffectResourceBinding::Kind::model,322,"sprites/blood.spr"});
    }
    void publish(unsigned record,double server,double application) {
        wf::stamp(state,record,server);wf::project(*assets,state,world);present(application);
    }
    void present(double application) {REQUIRE(assets->present_entities(host,application,1,world,{320,240}));}
    const entity_render::StudioEntityRenderInstance& instance(unsigned number=40) const {
        const auto all=world.entity_frame()->studio_instances();const auto found=std::ranges::find(all,number,
            &entity_render::StudioEntityRenderInstance::entity_number);REQUIRE(found!=all.end());return *found;
    }
};
}
