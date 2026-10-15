#include "local_resource_readiness_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "crossbow_test_fixture.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>

namespace {
namespace fixture=hlclient::tests;
namespace ready=fixture::readiness_fixture;
namespace app=hlclient::app;
namespace a=hlclient::assets;
namespace api=hlclient::game_api;
namespace c=hlclient::client;
namespace r=hlclient::renderer;
namespace g=hlclient::goldsrc;
using Catch::Approx;
fixture::SyntheticWad3Entry blood_texture(std::string_view name,bool yellow) {
    fixture::SyntheticWad3Entry entry;entry.name=name;entry.payload=fixture::synthetic_goldsrc_miptex(name,16,16);
    std::size_t offset=40;
    for(std::size_t level=0;level<4;++level) {
        const auto size=16U>>level;
        for(std::size_t y=0;y<size;++y)for(std::size_t x=0;x<size;++x)
            entry.payload[offset++]=(x>=size/4 && x<size*3/4 && y>=size/4 && y<size*3/4)?std::byte{220}:std::byte{0};
    }
    offset+=2;
    for(std::size_t color=0;color<255;++color)for(std::size_t channel=0;channel<3;++channel)
        entry.payload[offset+color*3+channel]=static_cast<std::byte>(255U-(color*255U/254U));
    entry.payload[offset+255U*3U]=std::byte{153};
    entry.payload[offset+255U*3U+1U]=yellow?std::byte{153}:std::byte{0};
    entry.payload[offset+255U*3U+2U]=std::byte{0};
    return entry;
}
struct CompositionFixture {
    fixture::ScopedLocalResourceTestRoot root;
    std::vector<std::byte> map;
    bool beam_resources{};
    explicit CompositionFixture(bool beams=false):beam_resources(beams) {
        fixture::SyntheticBspBuilder builder;
        const std::string worldspawn="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
        const auto bytes=std::as_bytes(std::span{worldspawn});
        builder.lump(fixture::SyntheticBspLumpId::entities).assign(bytes.begin(),bytes.end());
        std::array<fixture::SyntheticBspFace,2> faces{};
        for(auto& face:faces){face.light_styles={0,255,255,255};face.light_offset=0;}
        std::array<fixture::SyntheticBspModel,2> models{};models[1].first_face=1;models[1].visibility_leaf_count=0;
        builder.set_faces(faces).set_models(models);
        if(beams) {
            // The imported world floor and the inline model have the same
            // local Z=0 face. A committed Z=16 brush must intercept a ray
            // before the farther world floor, rather than borrowing its hit.
            std::array<fixture::SyntheticBspLeaf,2> leaves{};
            leaves[0].contents=-2;leaves[0].marksurface_count=0;
            fixture::SyntheticBspNode node;node.children={-2,-1};
            builder.set_nodes(std::array{node}).set_leaves(leaves);
        }
        builder.lump(fixture::SyntheticBspLumpId::lighting).assign(5U*5U*3U,std::byte{180});
        map=builder.build();root.write("valve","maps/test_map.bsp",map);
        fixture::SyntheticWad3Entry stone;stone.name="TEST_QUAD";stone.payload=fixture::synthetic_goldsrc_miptex(stone.name,64,64);
        root.write("valve","test.wad",fixture::synthetic_wad3({stone}).bytes);
        // Project-owned fixtures deliberately put yellow before red in WAD.
        root.write("valve","decals.wad",fixture::synthetic_wad3({blood_texture("{yblood1",true),blood_texture("{blood1",false)}).bytes);
        if(beams) {
            root.write("valve","decals.wad",fixture::synthetic_wad3({blood_texture("{shot1",false)}).bytes);
            root.write("valve","sprites/smoke.spr",fixture::sprite_fixture::literal_single_sprite(1));
            root.write("valve","sprites/hotglow.spr",fixture::sprite_fixture::literal_single_sprite(1));
        }
    }
    g::ResourceListState resources(std::uint16_t red=400,std::uint16_t yellow=7) const {
        if(beam_resources)return ready::parse_resource_list({
            {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(map.size()),0U},
            {2U,"*1",27U,0U,0U},{2U,"sprites/smoke.spr",75U,0U,0U},
            {2U,"sprites/hotglow.spr",76U,0U,0U}});
        return ready::parse_resource_list({
            {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(map.size()),0U},
            {2U,"*1",27U,0U,0U},
            {3U,"{yblood1",yellow,0U,0U},{3U,"{blood1",red,0U,0U}});
    }
    app::ReplayLocalAssetsCreateResult create() const {
        return app::RuntimeReplayLocalAssets::create(resources(),ready::parse_server_info("maps/test_map.bsp"),
            root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
    }
};
c::RuntimeClientObservationState observation(std::uint64_t ordinal=1,bool include=true,
    a::AssetVector3 origin={100,20,10},a::AssetVector3 angles={0,90,0},bool hidden=false) {
    c::RuntimeClientObservationState state;state.generation=1;state.publication_revision=ordinal;
    state.server_time_metadata.generation=state.client_metadata.generation=1;
    state.entity_metadata.generation=1;
    state.entity_metadata.freshness=c::RuntimeObservationFreshness::observed_in_record;
    state.entity_metadata.completeness=c::RuntimeObservationCompleteness::complete_reconstruction;
    state.entity_metadata.source=c::RuntimeObservationSource{ordinal,static_cast<std::size_t>(ordinal),42,0,8};
    if(include) {
        c::RuntimePacketEntityObservation entity;entity.entity_number=45;entity.ordinary_visual_schema=true;entity.model_index=27;
        entity.origin={origin.x,origin.y,origin.z};entity.angles={angles.x,angles.y,angles.z};entity.effects=hidden?128U:0U;
        state.packet_entities.push_back(entity);
    }
    state.canonical_state_hash=c::runtime_observation_canonical_hash(state);REQUIRE(c::valid_runtime_observation(state));return state;
}
void bind(api::GameClientHost& host,const g::ResourceListState& resources) {
    host.reset({1,1,1});host.set_movement_audio_time_origin(100);
    for(const auto& entry:resources.entries())if(entry.type()==g::ResourceType::decal)
        host.bind_effect_resource({api::EffectResourceBinding::Kind::decal,entry.index().value(),entry.name().bytes()});
}
c::CommittedTemporaryEffect event(std::uint32_t ordinal=1,std::int32_t index=400,std::int16_t entity=0,
    a::AssetVector3 origin={32,32,0}) {
    c::CommittedTemporaryEffect value;value.generation=1;value.record=1000U+ordinal;value.record_ordinal=ordinal;
    value.message_bit_offset=ordinal*32U;value.received_at_seconds=100.1;value.kind=c::TemporaryEffectKind::decal;
    value.origin=origin;value.decal_index=index;value.decal_entity=entity;
    value.decal_subtype=entity ? (index>=256 ? 118U : 104U) : (index>=256 ? 117U : 116U);return value;
}
api::TemporaryEffectIntent deliver(api::GameClientHost& host,const c::CommittedTemporaryEffect& value) {
    host.committed_temporary_effect(value);const auto batch=host.drain_temporary_effects(.1);
    REQUIRE(batch.count==1);REQUIRE(batch.effects[0].surface_decal);return batch.effects[0];
}
c::RuntimeClientObservationState door_observation(std::uint64_t ordinal=1,bool include=true,
    a::AssetVector3 origin={0,0,16},a::AssetVector3 angles={}) {
    auto value=observation(ordinal,include,origin,angles);
    if(include) {value.packet_entities[0].solid=4U;value.packet_entities[0].brush_move_type=7U;}
    value.canonical_state_hash=c::runtime_observation_canonical_hash(value);return value;
}
struct DoorGaussModule {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    DoorGaussModule() {
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        api::GameRecordState record;record.multiplayer.maximum_clients=8;host.commit_record(std::move(record));
        auto model=fixture::crossbow_fixture::model();model.resource_name="models/v_gauss.mdl";
        model.sequences.clear();model.supported_bodies[2]=model.selectable_bodies[2]=true;
        for(const auto name:{"idle","idle2","fidget","spinup","spin","fire","fire2","holster","draw"})
            model.sequences.push_back({30,31,std::string_view{name}=="spin",{},name});
        host.bind_model(model);
        auto state=fixture::crossbow_fixture::state(-1,1,100);state.weapon_hud.active_weapon_id=9;
        state.weapon_hud.clips[9]=-1;auto& type=state.weapon_hud.catalogue[0];
        type.id=9;type.command_name="weapon_gauss";type.slot=3;type.position=1;
        state.receiving_client->owned_weapon_bits=1U<<9;
        state.receiving_client->auxiliary_float2=0;state.receiving_client->auxiliary_float3=0;
        state.weapon_slots[0].weapon_id=9;state.weapon_slots[0].auxiliary_integer2=0;
        host.observe(state,0);
        std::array<api::ScriptedEventBinding,1> bindings{};bindings[0].index=900;
        constexpr std::string_view name="events/gauss.sc";
        std::copy(name.begin(),name.end(),bindings[0].name.begin());host.configure_scripted_events(bindings);
    }
    double fire(bool remote,bool primary) {
        if(remote) {
            auto value=fixture::crossbow_fixture::event(false,2);value.event_index=900;
            value.origin={32,32,36};value.angles={330,0,0}; // SDK model pitch -> downward aiming pitch.
            value.float_parameter1=primary ? 20.F : 200.F;
            value.boolean_parameter1=primary;value.boolean_parameter2=false;
            host.committed_scripted_event(value);return .03;
        }
        const auto command=[&](std::uint32_t sequence,double at,std::uint16_t buttons) {
            auto value=fixture::crossbow_fixture::command(sequence,at,buttons);
            value.shot_context->eye={32,32,64};value.shot_context->direction={0,0,-1};
            (void)host.command_movement_effect(value);
        };
        command(1,.02,primary ? std::uint16_t{1} : std::uint16_t{1U<<11});
        if(primary)return .02;
        command(2,.53,1U<<11);command(3,.62,0);return .62;
    }
};
}

TEST_CASE("E30 production app wrapper resolves shuffled server names and approved WAD into persistent world rendering", "[e30][server-decals][composition][approved-assets]") {
    CompositionFixture fixture;auto created=fixture.create();INFO((created.error?created.error->context:std::string{}));REQUIRE(created.projection);
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};bind(host,fixture.resources());
    const auto& prepared=created.projection->prepare_surface_decals(host.surface_decal_preparation());
    REQUIRE(prepared.archive_status==g::DecalAssetStatus::ready);REQUIRE(prepared.count==12U);
    REQUIRE(prepared.entries[0].status==g::DecalAssetStatus::ready);REQUIRE(prepared.entries[6].status==g::DecalAssetStatus::ready);
    CHECK(prepared.entries[0].texture->name=="{blood1");CHECK(prepared.entries[6].texture->name=="{yblood1");
    c::ClientWorldState world;REQUIRE(created.projection->reset_generation(observation(),world));
    app::WorldImpactPresentation owner;owner.bind_server_resources(1,9);
    for(const auto index:{400,7}) {
        const auto intent=deliver(host,event(static_cast<std::uint32_t>(index),index));
        const auto result=created.projection->materialize_server_decal(intent,owner,9);
        REQUIRE(result.asset_status==g::DecalAssetStatus::ready);REQUIRE(result.placement.status==app::ServerDecalStatus::submitted);
    }
    auto batches=owner.frame_batches(world.runtime_brushes().get());REQUIRE(batches.size()==2U);
    CHECK(batches[0].texture->name=="{blood1");CHECK(batches[1].texture->name=="{yblood1");
    CHECK(batches[0].texture->alpha_mode==a::WorldTextureAlphaMode::straight_alpha);
    CHECK(batches[0].material_mode==api::LocalDecalMaterialMode::lit_straight_alpha);
    owner.update(600);host.cancel_uncommitted();owner.retire_server_targets({});CHECK(owner.server_active()==2U);
    r::null::NullRenderer null;null.initialize();auto scene=c::build_render_scene(world);scene.additional_world_decals=batches;
    null.render(scene,{320,240});CHECK(null.statistics().last_decal_valid==2U);null.shutdown();
    // Remapped server indices choose the same named assets; neither SDK enum
    // nor WAD directory position is allowed to become the network mapping.
    api::GameClientHost remapped{hlclient::games::halflife::make_half_life_client_module()};bind(remapped,fixture.resources(7,400));
    const auto red=deliver(remapped,event(1,7));REQUIRE(red.surface_decal);CHECK(red.surface_decal->asset_slot==0U);
    const auto yellow=deliver(remapped,event(2,400));REQUIRE(yellow.surface_decal);CHECK(yellow.surface_decal->asset_slot==6U);
}
TEST_CASE("E30 normal app wrapper uses committed brush pose and refuses reuse before old delivery", "[e30][server-decals][composition][brush]") {
    CompositionFixture fixture;auto created=fixture.create();REQUIRE(created.projection);
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};bind(host,fixture.resources());
    (void)created.projection->prepare_surface_decals(host.surface_decal_preparation());
    c::ClientWorldState world;const auto first=observation();REQUIRE(created.projection->reset_generation(first,world));
    const auto birth=g::brush_models::make_brush_submodel_transform({100,20,10},{0,90,0});REQUIRE(birth);
    const auto point=g::brush_models::transform_brush_point(*birth.transform,{32,32,0});
    auto raw=event(1,400,45,point);raw.decal_target_pose=c::CommittedDecalTargetPose{27,{100,20,10},{0,90,0}};
    const auto intent=deliver(host,raw);
    // Pump a newer committed transform before delivering the earlier decal:
    // materialization must still use immutable birth pose, then draw current.
    REQUIRE(created.projection->project_entities(observation(2,true,{120,40,25},{15,30,10}),world));
    app::WorldImpactPresentation owner;owner.bind_server_resources(1,9);
    const auto result=created.projection->materialize_server_decal(intent,owner,9);
    REQUIRE(result.placement.status==app::ServerDecalStatus::submitted);
    auto batches=owner.frame_batches(world.runtime_brushes().get());REQUIRE(batches.size()==1U);
    for(const auto& vertex:*batches[0].vertices)CHECK(vertex.position.z==Approx(0).margin(1e-4));
    CHECK(batches[0].model_transform==world.runtime_brushes()->instances[0].model_transform);
    const auto geometry=batches[0].vertices;
    REQUIRE(created.projection->project_entities(observation(3,true,{120,40,25},{15,30,10},true),world));
    owner.retire_server_targets(created.projection->server_decal_lifetimes());
    CHECK(owner.server_active()==1U);CHECK(owner.frame_batches(world.runtime_brushes().get()).empty());
    REQUIRE(created.projection->project_entities(observation(4,true,{125,50,30},{0,45,0}),world));
    owner.retire_server_targets(created.projection->server_decal_lifetimes());batches=owner.frame_batches(world.runtime_brushes().get());
    REQUIRE(batches.size()==1U);CHECK(batches[0].vertices==geometry);
    REQUIRE(created.projection->project_entities(observation(5,false),world));
    owner.retire_server_targets(created.projection->server_decal_lifetimes());CHECK(owner.server_active()==0U);
    REQUIRE(created.projection->project_entities(observation(6),world));
    const auto rejected=created.projection->materialize_server_decal(intent,owner,9);
    CHECK(rejected.placement.status==app::ServerDecalStatus::target_unavailable);CHECK(owner.server_active()==0U);
    auto fresh=raw;fresh.record=1006;fresh.record_ordinal=6;fresh.message_bit_offset=192;
    const auto fresh_intent=deliver(host,fresh);REQUIRE(created.projection->materialize_server_decal(fresh_intent,owner,9).placement.status==app::ServerDecalStatus::submitted);
}
TEST_CASE("Normal asset projection supplies approved bullet texture and exact brush lifetime context",
          "[brush-weapon-impacts][composition][approved-assets]") {
    CompositionFixture fixture;
    fixture.root.write("valve","decals.wad",fixture::synthetic_wad3({blood_texture("{shot1",false)}).bytes);
    auto created=fixture.create();REQUIRE(created.projection);
    api::LocalImpactAssetProfile profile;
    constexpr char archive[]="decals.wad",name[]="{shot1";
    std::copy_n(archive,sizeof(archive),profile.wad_name.begin());std::copy_n(name,sizeof(name),profile.texture_name.begin());
    profile.material_mode=api::LocalDecalMaterialMode::white_neutral_modulate;
    REQUIRE(created.projection->prepare_impact_decal(profile)==app::LocalImpactDecalStatus::ready);
    c::ClientWorldState world;REQUIRE(created.projection->reset_generation(observation(),world));
    const auto first=created.projection->brush_impact_context();
    REQUIRE(first.library);REQUIRE(first.texture);CHECK(first.texture->name=="{shot1");
    CHECK(first.material_mode==profile.material_mode);REQUIRE(first.lifetimes.size()==1);
    CHECK(first.lifetimes[0].entity_number==45);CHECK(first.lifetimes[0].model_slot==27);
    CHECK(first.lifetimes[0].source_model_index==1);REQUIRE(first.lifetimes[0].lifetime!=0);
    const auto life=first.lifetimes[0].lifetime;
    REQUIRE(created.projection->project_entities(observation(2,true,{120,20,10},{0,90,0},true),world));
    CHECK(created.projection->brush_impact_context().lifetimes[0].lifetime==life);
    REQUIRE(created.projection->project_entities(observation(3,false),world));
    CHECK(created.projection->brush_impact_context().lifetimes.empty());
    REQUIRE(created.projection->project_entities(observation(4),world));
    CHECK(created.projection->brush_impact_context().lifetimes[0].lifetime!=life);
    CHECK_FALSE(created.projection->brush_impact_context(1).texture);
    CHECK_FALSE(created.projection->brush_impact_context(2).library);
}
TEST_CASE("E30 approved module app wrapper reaches production OpenGL with neutral blood background", "[e30][server-decals][composition][opengl][actual-context]") {
    auto gl=fixture::entity_opengl_fixture::try_context(320,240);if(!gl)SKIP("OpenGL3.3 context unavailable");gl->initialize_renderer();
    CompositionFixture fixture;auto created=fixture.create();REQUIRE(created.projection);
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};bind(host,fixture.resources());
    (void)created.projection->prepare_surface_decals(host.surface_decal_preparation());c::ClientWorldState world;
    REQUIRE(created.projection->reset_generation(observation(1,false),world));
    world.set_camera({{32,32,80},{32,32,0},{0,1,0}});
    auto scene=c::build_render_scene(world);gl->renderer().render(scene,{320,240});
    const auto before=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(before);
    app::WorldImpactPresentation owner;owner.bind_server_resources(1,9);const auto intent=deliver(host,event());
    REQUIRE(created.projection->materialize_server_decal(intent,owner,9).placement.status==app::ServerDecalStatus::submitted);
    scene.additional_world_decals=owner.frame_batches();gl->renderer().render(scene,{320,240});
    const auto after=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(after);
    CHECK(after.color_signature!=before.color_signature);CHECK(gl->renderer().decal_statistics().last_rendered==1U);
    const auto offset=(120U*320U+160U)*4U;CHECK(after.rgba8[offset]>after.rgba8[offset+1U]);
    for(const auto xy:std::array<std::array<std::size_t,2>,4>{{{147,107},{173,107},{147,133},{173,133}}}) {
        const auto p=(xy[1]*320U+xy[0])*4U;
        CHECK(std::equal(after.rgba8.begin()+p,after.rgba8.begin()+p+4U,before.rgba8.begin()+p));
    }
    owner.update(600);scene.additional_world_decals=owner.frame_batches();gl->renderer().render(scene,{320,240});
    const auto persistent=gl->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(persistent);CHECK(persistent.color_signature==after.color_signature);
    owner.reset();scene.additional_world_decals.clear();gl->renderer().render(scene,{320,240});
    const auto cleared=gl->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(cleared);CHECK(cleared.color_signature==before.color_signature);
    CHECK(glGetError()==GL_NO_ERROR);gl->release_renderer();
}

TEST_CASE("Normal brush impact collision is cached per committed publication without movement prediction",
          "[brush-weapon-impacts][composition][approved-assets][stationary-brush]") {
    CompositionFixture fixture;auto created=fixture.create();REQUIRE(created.projection);
    CHECK_FALSE(created.projection->impact_brush_scene()); // no committed entities yet
    const auto state=[](std::uint64_t ordinal,bool include=true,a::AssetVector3 origin={100,20,10}) {
        auto value=observation(ordinal,include,origin,{});
        if(include) {value.packet_entities[0].solid=4U;value.packet_entities[0].brush_move_type=7U;}
        value.canonical_state_hash=c::runtime_observation_canonical_hash(value);return value;
    };
    c::ClientWorldState world;REQUIRE(created.projection->reset_generation(state(1),world));
    const auto first=created.projection->impact_brush_scene();REQUIRE(first);REQUIRE(first->instances().size()==1);
    CHECK(first->instances()[0].identity.source_entity_index==45U);
    CHECK(first->instances()[0].identity.source_model_index==1U);
    CHECK(first->instances()[0].transform.translation.x==100);
    for(int repeat=0;repeat<4;++repeat)CHECK(created.projection->impact_brush_scene()==first);
    REQUIRE(created.projection->project_entities(state(2,true,{120,20,10}),world));
    const auto moved=created.projection->impact_brush_scene();REQUIRE(moved);CHECK(moved!=first);
    REQUIRE(moved->instances().size()==1);CHECK(moved->instances()[0].transform.translation.x==120);
    CHECK(first->instances()[0].transform.translation.x==100); // old owning snapshot is immutable
    REQUIRE(created.projection->project_entities(state(3,false),world));
    const auto removed=created.projection->impact_brush_scene();REQUIRE(removed);CHECK(removed->instances().empty());
    REQUIRE(created.projection->reset_generation(state(1),world));
    const auto reset=created.projection->impact_brush_scene();REQUIRE(reset);CHECK(reset!=first);
    REQUIRE(reset->instances().size()==1);CHECK(reset->instances()[0].transform.translation.x==100);
    auto missing_solidity=state(4);missing_solidity.packet_entities[0].solid.reset();
    missing_solidity.canonical_state_hash=c::runtime_observation_canonical_hash(missing_solidity);
    REQUIRE(created.projection->project_entities(missing_solidity,world));
    CHECK_FALSE(created.projection->impact_brush_scene()); // never retain a formerly valid pose on invalid input
    CHECK_FALSE(created.projection->impact_brush_scene());
}

TEST_CASE("Normal local and observer Gauss impacts stop at the current door and retain attached effects",
          "[door-all-impacts][brush-weapon-impacts][e21][composition][no-stock]") {
    for(const bool remote:{false,true})for(const bool primary:{true,false}) {
        CAPTURE(remote,primary);
        CompositionFixture fixture{true};auto created=fixture.create();REQUIRE(created.projection);
        c::ClientWorldState world;REQUIRE(created.projection->reset_generation(door_observation(),world));
        const auto probe=created.projection->trace({32,32,64},{32,32,-64});
        REQUIRE(probe.valid);REQUIRE(probe.hit);REQUIRE_FALSE(probe.unsupported);
        REQUIRE(probe.point.z==Approx(16).margin(.05)); // The farther world floor is Z=0.
        CHECK(probe.normal.z==Approx(1));
        DoorGaussModule module;const auto profile=module.host.local_impact_assets();REQUIRE(profile);
        REQUIRE(created.projection->prepare_impact_decal(*profile)==app::LocalImpactDecalStatus::ready);
        const auto now=module.fire(remote,primary);
        const auto effects=module.host.beam_effects(*created.projection,now,{});
        // A charged shot stopped by this solid half-space has its beam and
        // contact glow; free exit balls require a completed penetration.
        REQUIRE(effects.count>=2);if(primary)CHECK(effects.count>2);
        REQUIRE(effects.impact_count==1);
        REQUIRE(effects.sprites[0].ribbon);CHECK(effects.sprites[0].end.z==Approx(16).margin(.05));
        const auto glow=std::find_if(effects.sprites.begin(),effects.sprites.begin()+effects.count,
            [](const auto& frame){return frame.surface_normal.has_value();});
        REQUIRE(glow!=effects.sprites.begin()+effects.count);
        CHECK(glow->start.z==Approx(16).margin(.05));
        app::WorldImpactPresentation decals;decals.bind_server_resources(1,9);
        const auto context=created.projection->brush_impact_context();
        const auto hit=decals.submit(effects.impacts[0],created.projection->collision_world_package(),
            created.projection->surface_scene()->world_package().get(),created.projection->impact_brush_scene(),
            true,now,&context);
        REQUIRE(hit.status==app::WorldImpactStatus::hit);REQUIRE(hit.point);
        CHECK(hit.point->z==Approx(16).margin(.05));REQUIRE(decals.active()==1);
        CHECK_FALSE(decals.frame(context.texture,context.material_mode)); // Never mark the floor behind the door.
        auto batches=decals.frame_batches(world.runtime_brushes().get());REQUIRE(batches.size()==1);
        const auto geometry=batches[0].vertices;
        for(const auto& vertex:*geometry) {
            CHECK(vertex.position.z==Approx(0).margin(.001));
            CHECK(vertex.position.x>=0);CHECK(vertex.position.x<=64);
            CHECK(vertex.position.y>=0);CHECK(vertex.position.y<=64);
        }
        auto scene=c::build_render_scene(world);
        created.projection->present_entity_effects(scene,now,{effects.sprites.data(),effects.count});
        REQUIRE(scene.world_line_count==1);CHECK(scene.world_lines[0].end.z==Approx(16).margin(.05));
        REQUIRE(scene.follow_trail_entities);CHECK_FALSE(scene.follow_trail_entities->frame->sprite_instances().empty());
        scene=c::build_render_scene(world);
        created.projection->present_entity_effects(scene,now,std::span{&*glow,1});
        REQUIRE(scene.follow_trail_entities);REQUIRE(scene.follow_trail_entities->frame->sprite_instances().size()==1);
        const auto& initial_glow=scene.follow_trail_entities->frame->sprite_instances()[0];
        CHECK(initial_glow.orientation==a::SpriteOrientation::oriented);
        CHECK(initial_glow.transform.origin.z==Approx(16.125F).margin(.05));
        REQUIRE(glow->surface_attachment);
        // A finite shear can preserve the selected unit normal while moving
        // the contact incorrectly. It must not become an accepted door pose.
        auto invalid_scene=c::build_render_scene(world);
        auto invalid_frame=std::make_shared<hlclient::world_scene_render::RuntimeBrushRenderFrame>(*world.runtime_brushes());
        invalid_frame->instances[0].model_transform.values[4]+=.5F;
        invalid_scene.static_world->runtime_brushes=invalid_frame;
        created.projection->present_entity_effects(invalid_scene,now,std::span{&*glow,1});
        CHECK_FALSE(invalid_scene.follow_trail_entities);
        auto invalid_glow=*glow;invalid_glow.surface_attachment->generation=2;
        invalid_scene=c::build_render_scene(world);
        created.projection->present_entity_effects(invalid_scene,now,std::span{&invalid_glow,1});
        CHECK_FALSE(invalid_scene.follow_trail_entities);
        invalid_glow=*glow;invalid_glow.surface_attachment->local_normal.z=.5F;
        invalid_scene=c::build_render_scene(world);
        created.projection->present_entity_effects(invalid_scene,now,std::span{&invalid_glow,1});
        CHECK_FALSE(invalid_scene.follow_trail_entities);
        const auto repeated=module.host.beam_effects(*created.projection,now+.001,{});
        CHECK(repeated.impact_count==0);
        REQUIRE(decals.submit(effects.impacts[0],created.projection->collision_world_package(),
            created.projection->surface_scene()->world_package().get(),created.projection->impact_brush_scene(),
            true,now+.001,&context).status==app::WorldImpactStatus::hit);
        CHECK(decals.active()==1);

        const a::AssetVector3 moved_origin{8,4,24},moved_angles{10,30,0};
        REQUIRE(created.projection->project_entities(door_observation(2,true,moved_origin,moved_angles),world));
        decals.retire_server_targets(created.projection->server_decal_lifetimes());
        batches=decals.frame_batches(world.runtime_brushes().get());REQUIRE(batches.size()==1);
        CHECK(batches[0].vertices==geometry);
        REQUIRE(world.runtime_brushes());REQUIRE(world.runtime_brushes()->instances.size()==1);
        CHECK(batches[0].model_transform==world.runtime_brushes()->instances[0].model_transform);
        const auto moved=g::brush_models::make_brush_submodel_transform(moved_origin,moved_angles);REQUIRE(moved);
        const auto point=g::brush_models::transform_brush_point(*moved.transform,{32,32,0});
        const auto normal=g::brush_models::transform_brush_normal(*moved.transform,{0,0,1});
        scene=c::build_render_scene(world);
        created.projection->present_entity_effects(scene,now+.002,std::span{&*glow,1});
        REQUIRE(scene.follow_trail_entities);REQUIRE(scene.follow_trail_entities->frame->sprite_instances().size()==1);
        const auto& attached=scene.follow_trail_entities->frame->sprite_instances()[0].transform.origin;
        CHECK(attached.x==Approx(point.x+normal.x*.125F).margin(.05));
        CHECK(attached.y==Approx(point.y+normal.y*.125F).margin(.05));
        CHECK(attached.z==Approx(point.z+normal.z*.125F).margin(.05));
        REQUIRE(created.projection->project_entities(door_observation(3,false),world));
        decals.retire_server_targets(created.projection->server_decal_lifetimes());CHECK(decals.active()==0);
        scene=c::build_render_scene(world);
        created.projection->present_entity_effects(scene,now+.003,std::span{&*glow,1});CHECK_FALSE(scene.follow_trail_entities);
        const auto floor=created.projection->trace({32,32,64},{32,32,-64});
        REQUIRE(floor.valid);REQUIRE(floor.hit);CHECK(floor.point.z==Approx(0).margin(.05));
        REQUIRE(created.projection->project_entities(door_observation(4),world));
        CHECK(decals.frame_batches(world.runtime_brushes().get()).empty());
        scene=c::build_render_scene(world);
        created.projection->present_entity_effects(scene,now+.004,std::span{&*glow,1});CHECK_FALSE(scene.follow_trail_entities);
    }
}

TEST_CASE("Beam impacts cannot replace missing or malformed committed door collision with a farther wall",
          "[door-all-impacts][brush-weapon-impacts][e21][composition][no-stock][negative]") {
    for(const bool committed:{false,true}) {
        CAPTURE(committed);CompositionFixture fixture{true};auto created=fixture.create();REQUIRE(created.projection);
        c::ClientWorldState world;
        if(committed) {
            REQUIRE(created.projection->reset_generation(door_observation(),world));
            REQUIRE(created.projection->trace({32,32,64},{32,32,-64}).valid);
            auto malformed=door_observation(2);malformed.packet_entities[0].solid.reset();
            malformed.canonical_state_hash=c::runtime_observation_canonical_hash(malformed);
            REQUIRE(created.projection->project_entities(malformed,world));
        }
        CHECK_FALSE(created.projection->impact_brush_scene());
        const auto rejected=created.projection->trace({32,32,64},{32,32,-64});
        CHECK_FALSE(rejected.valid);
        DoorGaussModule module;const auto now=module.fire(false,true);
        const auto effects=module.host.beam_effects(*created.projection,now,{});
        CHECK(effects.count==0);CHECK(effects.impact_count==0);CHECK(effects.rejected>0);
    }
}

TEST_CASE("Door Gauss decal and contact glow independently reach the normal OpenGL passes",
          "[door-all-impacts][brush-weapon-impacts][e21][composition][opengl][actual-context][no-stock]") {
    auto gl=fixture::entity_opengl_fixture::try_context(320,240);if(!gl)SKIP("OpenGL3.3 context unavailable");gl->initialize_renderer();
    CompositionFixture fixture{true};auto created=fixture.create();REQUIRE(created.projection);
    c::ClientWorldState world;REQUIRE(created.projection->reset_generation(door_observation(),world));
    world.set_camera({{32,32,80},{32,32,16},{0,1,0}});
    DoorGaussModule module;const auto profile=module.host.local_impact_assets();REQUIRE(profile);
    REQUIRE(created.projection->prepare_impact_decal(*profile)==app::LocalImpactDecalStatus::ready);
    const auto now=module.fire(false,true);const auto effects=module.host.beam_effects(*created.projection,now,{});
    REQUIRE(effects.impact_count==1);
    const auto glow=std::find_if(effects.sprites.begin(),effects.sprites.begin()+effects.count,
        [](const auto& frame){return frame.surface_normal.has_value();});
    REQUIRE(glow!=effects.sprites.begin()+effects.count);
    app::WorldImpactPresentation decals;decals.bind_server_resources(1,9);
    const auto context=created.projection->brush_impact_context();
    REQUIRE(decals.submit(effects.impacts[0],created.projection->collision_world_package(),
        created.projection->surface_scene()->world_package().get(),created.projection->impact_brush_scene(),
        true,now,&context).status==app::WorldImpactStatus::hit);
    auto scene=c::build_render_scene(world);
    const auto draw=[&] {gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        const auto result=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(result);return result;};
    const auto baseline=draw();
    scene.additional_world_decals=decals.frame_batches(world.runtime_brushes().get());REQUIRE(scene.additional_world_decals.size()==1);
    const auto decal=draw();CHECK(decal.color_signature!=baseline.color_signature);
    CHECK(gl->renderer().decal_statistics().last_rendered==1);
    scene=c::build_render_scene(world);
    created.projection->present_entity_effects(scene,now,std::span{&*glow,1});REQUIRE(scene.follow_trail_entities);
    const auto sprite=draw();CHECK(sprite.color_signature!=baseline.color_signature);
    // Neither a beam nor free particles can mask a missing contact sprite.
    CHECK(scene.world_line_count==0);CHECK(scene.world_flash_count==0);
    scene=c::build_render_scene(world);CHECK(draw().rgba8==baseline.rgba8);
    gl->release_renderer();
}
