#pragma once
#include "goldsrc_studio_test_fixture.hpp"
#include "python_357_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>

namespace hlclient::tests::python_fixture {
// Independently authored triangles and animation tracks. Only the inspected
// public p-model socket's names/bounds are used, never installed mesh bytes.
inline std::vector<std::byte> remote_model(bool weapon) {
    auto bytes=literal_minimal_goldsrc_studio_v10();
    constexpr std::array names{"Bip01","Bip01 Pelvis","Bip01 Spine","Bip01 Spine1","Bip01 Spine2",
        "Bip01 Spine3","Bip01 Neck","Bip01 R Arm","Bip01 R Arm1","Bip01 R Arm2","Bip01 R Hand","Cylinder01"};
    const auto bones=bytes.size();bytes.resize(bytes.size()+12*112);
    studio_write_i32le(bytes,140,12);studio_write_i32le(bytes,144,static_cast<int>(bones));
    studio_write_i32le(bytes,212,0);studio_write_i32le(bytes,216,0);
    for(std::size_t i=0;i<12;++i) {
        const auto at=bones+i*112;studio_write_fixed_string(bytes,at,32,!weapon && i==11 ? "owned_body" : names[i]);
        studio_write_i32le(bytes,at+32,static_cast<int>(i)-1);
        for(std::size_t axis=0;axis<6;++axis) {
            studio_write_i32le(bytes,at+40+axis*4,-1);studio_write_f32le(bytes,at+88+axis*4,1);
        }
    }
    if(weapon) {
        studio_write_vector3(bytes,bones+11*112+64,5.854939F,.808541F,2.847965F);
        studio_write_vector3(bytes,bones+11*112+76,-1.564202F,-3.113941F,-1.474388F);
        studio_write_vector3(bytes,kSyntheticStudioVerticesOffset,-1.06F,-4.49F,-3.67F);
        studio_write_vector3(bytes,kSyntheticStudioVerticesOffset+12,1.05F,1.56F,-3.67F);
        studio_write_vector3(bytes,kSyntheticStudioVerticesOffset+24,0,0,8.06F);
        for(std::size_t i=0;i<3;++i) {
            bytes[kSyntheticStudioVertexBonesOffset+i]=std::byte{11};bytes[kSyntheticStudioNormalBonesOffset+i]=std::byte{11};
        }
    } else {
        studio_write_vector3(bytes,kSyntheticStudioVerticesOffset,0,-5,-12);
        studio_write_vector3(bytes,kSyntheticStudioVerticesOffset+12,0,5,-12);
        studio_write_vector3(bytes,kSyntheticStudioVerticesOffset+24,0,0,12);
        const auto controllers=bytes.size();bytes.resize(bytes.size()+4*24);
        studio_write_i32le(bytes,148,4);studio_write_i32le(bytes,152,static_cast<int>(controllers));
        for(std::size_t i=0;i<4;++i) {
            const auto at=controllers+i*24;studio_write_i32le(bytes,at,static_cast<int>(i+3));
            studio_write_i32le(bytes,at+4,8);studio_write_f32le(bytes,at+8,-45);studio_write_f32le(bytes,at+12,45);
            studio_write_i32le(bytes,at+20,static_cast<int>(i));studio_write_i32le(bytes,bones+(i+3)*112+52,static_cast<int>(i));
        }
    }
    constexpr std::array labels{"idle","walk","crouch","shootpython","die"};
    const auto count=weapon ? 1U : labels.size(),seqs=bytes.size();bytes.resize(bytes.size()+count*176);
    studio_write_i32le(bytes,164,static_cast<int>(count));studio_write_i32le(bytes,168,static_cast<int>(seqs));
    for(std::size_t i=0;i<count;++i) {
        const auto at=seqs+i*176,anim=bytes.size();bytes.resize(bytes.size()+12*12+10);
        studio_write_fixed_string(bytes,at,32,labels[i]);studio_write_f32le(bytes,at+32,10);
        studio_write_i32le(bytes,at+36,i<3 ? 1 : 0);studio_write_i32le(bytes,at+56,4);
        studio_write_vector3(bytes,at+96,-32,-32,-32);studio_write_vector3(bytes,at+108,32,32,32);
        studio_write_i32le(bytes,at+120,1);studio_write_i32le(bytes,at+124,static_cast<int>(anim));studio_write_i32le(bytes,at+152,-1);
        if(!weapon) {
            const auto track=anim+10*12,stream=anim+12*12;studio_write_u16le(bytes,track+2,static_cast<std::uint16_t>(stream-track));
            bytes[stream]=bytes[stream+1]=std::byte{4};
            for(std::size_t frame=0;frame<4;++frame)studio_write_i16le(bytes,stream+2+frame*2,static_cast<std::int16_t>(i*6+frame));
        }
    }
    studio_write_i32le(bytes,72,static_cast<int>(bytes.size()));return bytes;
}

inline void remote_gpu(app::RuntimeReplayLocalAssets& projection,assets::AssetVector3 origin,
    const std::array<std::uint32_t,4>& sequences) {
    const auto player=projection.studio_model_data(7),weapon=projection.studio_model_data(71);REQUIRE(player);REQUIRE(weapon);
    auto gl=entity_opengl_fixture::try_context(640,480);
    if(!gl || !entity_opengl_fixture::capable_context()) SKIP("Remote Python OpenGL 3.3 unavailable");
    gl->initialize_renderer();Module module;client::ClientWorldState world;
    world.set_camera({{origin.x+96,origin.y-64,origin.z+36},{origin.x,origin.y,origin.z+4},{0,0,1}});
    for(std::size_t step=0;step<6;++step) {
        CAPTURE(step);const auto ordinal=step+10;auto state=module.observed;
        state.publication_revision=ordinal;state.entity_metadata={1,client::RuntimeObservationFreshness::observed_in_record,
            client::RuntimeObservationCompleteness::complete_reconstruction,client::RuntimeObservationSource{ordinal,ordinal,0,0,8}};
        state.server_time_metadata=state.entity_metadata;state.server_time_seconds=1.+step*.2;
        state.client_metadata=state.entity_metadata;
        state.packet_entities.clear();client::RuntimePacketEntityObservation entity;
        entity.entity_number=2;entity.ordinary_visual_schema=entity.player_movement_schema=true;entity.model_index=7;
        entity.origin={origin.x+step*2.,origin.y,origin.z};entity.angles={0.,0.,0.};
        entity.sequence=sequences[step==3 ? 3 : step<3 ? step : 0];entity.frame=64.;entity.body=0;entity.skin=0;
        entity.animation_time_seconds=state.server_time_seconds;entity.frame_rate=1.;entity.gait_sequence=0;
        entity.controllers.fill(127);entity.blending.fill(127);entity.weapon_model_index=step==3 || step==5 ? 0 : 71;
        state.packet_entities.push_back(entity);state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
        REQUIRE(client::valid_runtime_observation(state));
        if(step==0) REQUIRE(projection.reset_generation(state,world));else REQUIRE(projection.project_entities(state,world));
        const auto anchor=100.+step*.3,now=anchor+.21;
        REQUIRE(projection.present_entities(module.host,anchor,1,world,{640,480}));
        REQUIRE(projection.present_entities(module.host,now,1,world,{640,480}));
        const auto frame=world.entity_frame();REQUIRE(frame);const bool attached=step!=3 && step!=5;
        REQUIRE(frame->studio_instances().size()==(attached ? 2U : 1U));
        if(!attached) {CHECK(projection.remote_weapon_poses()[0].source==api::RemoteSocketSource::unavailable);continue;}
        const auto& parent=frame->studio_instances()[0];const auto& child=frame->studio_instances()[1];CHECK(child.body_value==0);
        const auto& child_pose=frame->studio_poses()[child.pose_index].bone_matrices;
        const auto& parent_pose=frame->studio_poses()[parent.pose_index].bone_matrices;REQUIRE(child_pose.size()==12);
        for(std::size_t bone=0;bone<11;++bone) {
            const auto found=std::ranges::find(player->bones,weapon->bones[bone].name,&assets::ModelBone::name);
            REQUIRE(found!=player->bones.end());CHECK(child_pose[bone]==parent_pose[static_cast<std::size_t>(found-player->bones.begin())]);
        }
        REQUIRE(projection.remote_weapon_poses().size()==1);const auto socket=projection.remote_weapon_poses()[0];
        CHECK(socket.semantic_key==3);CHECK(socket.server_sequence==entity.sequence);
        CHECK(socket.source==api::RemoteSocketSource::validated_pmodel_socket);
        auto event=python_fixture::event(2,ordinal,now);event.weapon_model_index=71;
        module.host.committed_scripted_event(event);const auto effects=module.host.drain_remote_effects(now);REQUIRE(effects.count==1);
        REQUIRE(effects.effects[0].flash);CHECK_FALSE(effects.effects[0].shell);CHECK_FALSE(effects.effects[0].impact->tracer);
        CHECK(same(effects.effects[0].muzzle_origin,socket.muzzle.origin));
        REQUIRE(projection.present_entities(module.host,now,1,world,{640,480}));
        CHECK(world.entity_frame()->studio_poses()[parent.pose_index].bone_matrices==parent_pose);
        auto scene=client::build_render_scene(world);scene.static_world.reset();
        const auto draw=[&]{gl->renderer().render(scene,{640,480});CHECK(glGetError()==GL_NO_ERROR);
            return gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);};
        const auto full=draw();REQUIRE(full);const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
        entity_render::EntityRenderFrameBuildInput body;body.resource_id=1;body.resource_revision=1;
        body.interpolation=scene.dynamic_entities->frame->interpolation();body.studio_instances.push_back(parent);
        body.studio_poses.assign(frame->studio_poses().begin(),frame->studio_poses().end());
        auto built=entity_render::EntityRenderFrameBuilder{}.build(*scene.dynamic_entities->package,std::move(body));REQUIRE(built);
        scene.dynamic_entities->frame=std::make_shared<const entity_render::EntityRenderFrame>(std::move(*built.frame));
        CHECK_FALSE(std::ranges::equal(full.rgba8,draw().rgba8));
        CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    }
    gl->release_renderer();module.host.teardown();
}
}
