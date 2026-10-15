#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/remote_player.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_source_bundle.hpp>

#include "local_resource_readiness_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
namespace api=hlclient::game_api;
namespace hl=hlclient::games::halflife;
namespace assets=hlclient::assets;
namespace client=hlclient::client;
namespace studio=hlclient::goldsrc::studio;
using Catch::Approx;

assets::SkeletalModelAssetData player_model() {
    assets::SkeletalModelAssetData model;
    constexpr std::array names{"Bip01","Bip01 Pelvis","Bip01 Spine","Bip01 Spine1",
        "Bip01 Spine2","Bip01 Spine3","Bip01 L Thigh","Bip01 R Thigh"};
    constexpr std::array parents{-1,0,1,2,3,4,1,1};
    for(std::size_t i=0;i<names.size();++i) {
        assets::ModelBone bone;
        bone.name=names[i]; bone.parent_index=parents[i];
        if(i>=2U && i<6U) bone.controller_indices[5]=static_cast<std::int32_t>(i-2U);
        model.bones.push_back(bone);
    }
    for(std::int32_t i=0;i<4;++i)
        model.bone_controllers.push_back({i+2,0x20U,-30.0F,30.0F,127,i,false});
    model.sequence_groups={{"project_owned",{},0U,false}};
    for(std::size_t i=0;i<3U;++i) {
        assets::ModelSequence sequence;
        sequence.label=i==0U ? "project_aim" : i==1U ? "project_gait" : "project_shoot";
        sequence.frame_count=11U; sequence.frames_per_second=i==2U ? 20.0F : 10.0F;
        sequence.source_flags=1U;
        sequence.blend_count=i==1U ? 1U : 2U;
        sequence.blend_start={-45.0F,0.0F}; sequence.blend_end={45.0F,0.0F};
        sequence.blend_types={0x8,0};
        sequence.motion_bone=0;
        if(i==1U) sequence.linear_movement.x=120.0F;
        for(std::uint32_t blend=0U;blend<sequence.blend_count;++blend) {
            assets::ModelAnimationBlend animation;
            animation.source_blend_ordinal=blend;
            for(std::uint32_t bone=0U;bone<model.bones.size();++bone) {
                assets::ModelBoneAnimationTrack track;
                track.bone_index=bone;
                for(std::size_t channel=0U;channel<track.channels.size();++channel) {
                    track.channels[channel].semantic=
                        static_cast<assets::ModelAnimationChannelSemantic>(channel);
                    track.channels[channel].frame_coverage=sequence.frame_count;
                }
                // Distinct constant X tracks make the source of lower/upper
                // bones independently visible. Z has a project-owned ramp.
                track.channels[0].source_default=static_cast<float>(
                    (i==0U ? 10U : i==1U ? 100U : 30U)+bone+2U*blend);
                track.channels[2].source_scale=1.0F;
                track.channels[2].runs={{0U,11U,11U,{0,1,2,3,4,5,6,7,8,9,10}}};
                animation.bone_tracks.push_back(std::move(track));
            }
            sequence.animation_blends.push_back(std::move(animation));
        }
        model.sequences.push_back(sequence);
    }
    model.textures.resize(1U);
    model.textures[0].source_name="project_owned_skin";
    model.textures[0].width=model.textures[0].height=1U;
    model.textures[0].rgba8_level_zero={std::byte{40U},std::byte{80U},std::byte{120U},std::byte{255U}};
    model.skin_families.push_back({{0U}});
    model.bodyparts={{"project_owned_body",1,{0U}}};
    model.submodels.resize(1U);
    for(std::uint32_t bone=0U;bone<model.bones.size();++bone)
        model.submodels[0].vertices.push_back({{0,0,0},{0,0,1},0,0,bone,bone});
    model.submodels[0].indices={0U,1U,7U};
    model.submodels[0].meshes={{0U,3U,0U,0U,1U,1U,0U}};
    return model;
}
client::RuntimePacketEntityObservation player(std::uint32_t number=2U) {
    client::RuntimePacketEntityObservation result;
    result.entity_number=number; result.origin={0.0,0.0,0.0}; result.angles={0.0,0.0,0.0};
    result.ordinary_visual_schema=result.player_movement_schema=true;
    result.model_index=7U; result.sequence=0U; result.frame=0.0;
    result.animation_time_seconds=10.0; result.frame_rate=1.0; result.gait_sequence=1U;
    result.body=0U; result.skin=0;
    for(auto& controller:result.controllers) controller=127U;
    for(auto& blending:result.blending) blending=127U;
    return result;
}
api::RemotePlayerPresentationContext context(const assets::SkeletalModelAssetData& model,
    const client::RuntimePacketEntityObservation& sample,
    const client::RuntimePacketEntityObservation& current,
    const client::RuntimePacketEntityObservation* previous=nullptr,
    std::uint64_t record=1U,double now=10.0,double sample_time=10.0) {
    return {1U,1U,1U,27U,{19U,31U},model,sample,current,previous,32U,1U,
        record,previous ? record-1U : 0U,now,previous ? now-0.1 : now,sample_time,false,
        record,previous ? record-1U : 0U};
}
void seed(hl::HalfLifeRemotePlayerPresentation& presentation,
    const assets::SkeletalModelAssetData& model,
    const client::RuntimePacketEntityObservation& entity) {
    REQUIRE(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::ready);
}

studio::StudioPoseCompositionInput composition(const api::RemotePlayerPresentationIntent& intent) {
    const auto convert=[](const api::RemotePlayerStudioSample& sample) {
        studio::StudioPoseInput result;
        result.compatibility_profile=studio::StudioPoseCompatibilityProfile::
            public_goldsrc48_discrete_local_asset_v1;
        result.sequence_index=sample.sequence; result.frame_coordinate=sample.frame_coordinate;
        result.body_value=sample.body; result.skin_family_index=sample.skin;
        result.controller_values=sample.controllers; result.blending_values=sample.blending;
        return result;
    };
    studio::StudioPoseCompositionInput result;
    result.main=convert(intent.sample);
    if(intent.previous_sample) result.previous=convert(*intent.previous_sample);
    result.previous_weight=static_cast<float>(intent.previous_weight);
    if(intent.gait_sample) {
        result.layer=convert(*intent.gait_sample);
        for(std::size_t i=0U;i<intent.bone_count;++i)
            if(intent.gait_bone_mask[i]) result.lower_bone_indices.push_back(static_cast<std::uint32_t>(i));
    }
    return result;
}
} // namespace

TEST_CASE("E13 server proxy classification requires exact neutral fields and current referenced player",
    "[e13][game][remote-player][corpse]") {
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    auto source=player();auto corpse=source;
    corpse.entity_number=40;corpse.player_movement_schema=false;corpse.model_index=99;
    std::array entities{source,corpse};
    const auto select=[&] {return host.world_entity({corpse,nullptr,entities,8});};
    CHECK(select().role==api::WorldEntityPresentationRole::ordinary);
    corpse.render_effect=17;corpse.render_amount=2;
    REQUIRE(select().role==api::WorldEntityPresentationRole::player_proxy);
    CHECK(select().model_index==7);CHECK(select().source_player==2);CHECK(select().discontinuity);
    for(const auto invalid:{0U,9U,255U}) {
        corpse.render_amount=invalid;CHECK(select().role==api::WorldEntityPresentationRole::unavailable);
    }
    corpse.render_amount.reset();CHECK(select().role==api::WorldEntityPresentationRole::unavailable);
    corpse.render_amount=2;entities[0].player_movement_schema=false;
    CHECK(select().role==api::WorldEntityPresentationRole::unavailable);
    entities[0]=source;entities[0].model_index.reset();
    CHECK(select().role==api::WorldEntityPresentationRole::unavailable);
    entities[0]=source;corpse.render_effect=16;
    CHECK(select().role==api::WorldEntityPresentationRole::ordinary);
    corpse.render_effect=17;source.weapon_model_index=21;
    entities[0]=source;host.remote_entities(entities,8,1);
    entities[0].model_index=0;entities[0].weapon_model_index=0;entities[0].gait_sequence=0;
    host.remote_entities(entities,8,2);
    REQUIRE(select().role==api::WorldEntityPresentationRole::player_proxy);
    CHECK(select().model_index==7); // Pinned player hidden after CopyToBodyQue, selection remains this identity's.
    host.remote_slot_boundary(2);
    CHECK(select().role==api::WorldEntityPresentationRole::unavailable);
    entities[0]=source;host.remote_entities(entities,8,3);
    REQUIRE(select().role==api::WorldEntityPresentationRole::player_proxy);
    host.reset({2,2,1});entities[0].model_index=0;entities[0].weapon_model_index=0;entities[0].gait_sequence=0;
    host.remote_entities(entities,8,1);
    CHECK(select().role==api::WorldEntityPresentationRole::unavailable); // No previous-map selection.
    host.teardown();CHECK(select().role==api::WorldEntityPresentationRole::ordinary);
}

TEST_CASE("E13 authoritative death clamps and retires weapon then independent corpse and respawn across three lives",
    "[e13][game][remote-player][corpse][lifecycle]") {
    auto model=player_model();model.sequences[2].source_flags=0; // owned terminal track, not a label heuristic
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    auto alive=player();alive.weapon_model_index=21;
    api::GameRecordState notice_state;
    client::RuntimeLifeEvent notice;notice.kind=client::RuntimeLifeEventKind::death_notice;
    notice.victim_entity=2;notice.source={1,1,1,0,8};notice_state.life_events.push_back(notice);
    host.commit_record(std::move(notice_state));
    const auto diagnostic_only=host.remote_player(context(model,alive,alive));
    CHECK(diagnostic_only.death_notice_source==notice.source);
    REQUIRE(diagnostic_only.gait_sample);CHECK(diagnostic_only.attachment_model_index==21);
    std::uint64_t record=1;
    for(unsigned cycle=0;cycle<3;++cycle) {
        const double at=10.0+static_cast<double>(cycle)*2;
        alive.animation_time_seconds=at;alive.origin.x=static_cast<double>(cycle)*100;
        std::array entities{alive};host.remote_entities(entities,8,record);
        REQUIRE(host.remote_player(context(model,alive,alive,nullptr,record++,at,at)).gait_sample);
        auto dead=alive;dead.sequence=2;dead.gait_sequence=0;dead.weapon_model_index=0;
        dead.animation_time_seconds=at+0.1;dead.frame=0;
        entities[0]=dead;host.remote_entities(entities,8,record);
        // Bounded position sample still carries old attachment: committed zero must win now.
        const auto dying=host.remote_player(context(model,alive,dead,&alive,record++,at+0.1,at+0.1));
        REQUIRE(dying.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(dying.sample.sequence==2);CHECK_FALSE(dying.gait_sample);CHECK_FALSE(dying.attachment_model_index);
        const auto terminal=host.remote_player(context(model,dead,dead,nullptr,record-1,at+0.1,at+0.35));
        CHECK(terminal.sample.frame_coordinate==Approx(5.0));
        dead.frame=256;
        const auto final=host.remote_player(context(model,dead,dead,nullptr,record++,at+0.4,at+0.5));
        CHECK(final.sample.frame_coordinate==Approx(10.0));CHECK_FALSE(final.previous_sample);
        auto corpse=dead;corpse.entity_number=40;corpse.player_movement_schema=false;
        corpse.render_effect=17;corpse.render_amount=2;corpse.effects=32;corpse.origin.x=-100;
        auto proxy_context=context(model,corpse,corpse,nullptr,record++,at+0.6,at+0.6);
        proxy_context.server_owned_proxy=true;
        const auto proxy=host.remote_player(proxy_context);
        REQUIRE(proxy.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(proxy.sample.sequence==2);CHECK(proxy.sample.frame_coordinate==Approx(10));
        CHECK_FALSE(proxy.previous_sample);CHECK_FALSE(proxy.gait_sample);CHECK_FALSE(proxy.attachment_model_index);
        auto respawn=alive;respawn.origin.x=500+cycle;respawn.effects=32;respawn.animation_time_seconds=at+0.7;
        entities[0]=respawn;host.remote_entities(entities,8,record);
        auto fresh_context=context(model,respawn,respawn,&dead,record++,at+0.7,at+0.7);
        fresh_context.discontinuity=true;
        const auto fresh=host.remote_player(fresh_context);
        REQUIRE(fresh.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(fresh.sample.sequence==0);REQUIRE(fresh.gait_sample);
        CHECK(fresh.gait_sample->frame_coordinate==Approx(0));CHECK_FALSE(fresh.previous_sample);
        CHECK(fresh.attachment_model_index==21);
        CHECK(fresh.visual_epoch>dying.visual_epoch);
        // Sampling the same server-owned corpse cannot inherit the living player's animation clock.
        const auto unchanged=host.remote_player(proxy_context);CHECK(unchanged.sample==proxy.sample);
        alive=respawn;alive.effects=0;
    }
    host.remote_entities({},8,record++);
    const std::array rejoined{alive};host.remote_entities(rejoined,8,record++);
    const auto rejoin=host.remote_player(context(model,alive,alive,nullptr,record,16,16));
    REQUIRE(rejoin.gait_sample);CHECK(rejoin.gait_sample->frame_coordinate==Approx(0));
}

TEST_CASE("E13 production Studio and OpenGL show death terminal then separate corpse and fresh living pose without reupload",
    "[e13][remote-player][corpse][actual-context][opengl]") {
    namespace er=hlclient::entity_render;
    namespace renderer=hlclient::renderer;
    namespace ef=hlclient::tests::entity_render_fixture;
    auto model=player_model();model.sequences[2].source_flags=0;
    model.submodels[0].vertices={
        {{0,-8,-8},{1,0,0},0,0,0,0},{{0,8,-8},{1,0,0},0,0,0,0},{{0,0,8},{1,0,0},0,0,0,0}};
    model.submodels[0].indices={0,1,2,0,2,1};
    model.submodels[0].meshes={{0,6,0,0,1,1,0}};
    model.source_clipping_bounds={{-1,-8,-8},{1,8,8}};
    auto source=std::make_shared<assets::ModelAsset>();source->identity.source_name="owned_e13_player";
    source->skeletal_data=std::make_shared<const assets::SkeletalModelAssetData>(model);
    auto built=ef::scene_package(ef::render_assets(true,false,assets::SpriteTextureFormat::normal,
        false,assets::SpriteOrientation::view_parallel,source));REQUIRE(built);
    auto package=std::make_shared<const er::EntitySceneRenderPackage>(std::move(*built.package));
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!gl) SKIP("E13 OpenGL 3.3 Core unavailable");gl->initialize_renderer();
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    const studio::StudioPoseModelIdentity identity{"owned_e13_player",{19,31}};
    std::uint64_t revision=1;
    const auto scene_for=[&](std::span<const api::RemotePlayerPresentationIntent> intents,
        std::span<const assets::AssetVector3> origins) {
        er::EntityRenderFrameBuildInput input;input.resource_id=777;input.resource_revision=revision++;
        input.interpolation={0,0,0,0,1,1,er::EntityRenderInterpolationProfile::synthetic_seconds_v1};
        for(std::size_t i=0;i<intents.size();++i) {
            const auto pose=studio::StudioPoseEvaluator{}.compose(identity,model,composition(intents[i]));REQUIRE(pose);
            er::StudioRenderPose palette{package->studio_assets()[0]->source_identity(),{}};
            for(const auto& bone:pose.pose->world_bones()) {
                const auto& m=bone.transform.values;
                palette.bone_matrices.push_back({m[0],m[4],m[8],0,m[1],m[5],m[9],0,
                    m[2],m[6],m[10],0,m[3],m[7],m[11],1});
            }
            input.studio_poses.push_back(std::move(palette));
            er::StudioEntityRenderInstance instance;instance.entity_number=static_cast<std::uint32_t>(i+2);
            instance.studio_asset_index=0;instance.pose_index=static_cast<std::uint32_t>(i);
            instance.transform.origin=origins[i];instance.static_light_rgb=std::array<float,3>{0.4F,0.4F,0.4F};
            const auto bounds=studio::StudioPoseEvaluator{}.posed_bounds(identity,model,*pose.pose);REQUIRE(bounds);
            instance.interpolated_bounds={{bounds.bounds->minimum.x+origins[i].x,bounds.bounds->minimum.y+origins[i].y,
                bounds.bounds->minimum.z+origins[i].z},{bounds.bounds->maximum.x+origins[i].x,
                bounds.bounds->maximum.y+origins[i].y,bounds.bounds->maximum.z+origins[i].z}};
            input.studio_instances.push_back(instance);
        }
        auto frame=er::EntityRenderFrameBuilder{}.build(*package,std::move(input));REQUIRE(frame);
        renderer::RenderScene scene;scene.camera.position={200,0,0};scene.camera.target={0,0,0};scene.camera.up={0,0,1};
        scene.dynamic_entities=renderer::RenderDynamicEntities{package,
            std::make_shared<const er::EntityRenderFrame>(std::move(*frame.frame)),{}};
        return scene;
    };
    const auto image=[&](const renderer::RenderScene& scene) {
        gl->renderer().render(scene,{320,240});
        auto result=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(result);
        CHECK(glGetError()==GL_NO_ERROR);return result;
    };
    auto alive=player();alive.weapon_model_index=21;
    const auto alive_intent=host.remote_player(context(model,alive,alive));REQUIRE(alive_intent.gait_sample);
    const std::array center{assets::AssetVector3{0,0,0}};
    const std::array alive_intents{alive_intent};const auto alive_scene=scene_for(alive_intents,center);
    const auto alive_image=image(alive_scene);CHECK(alive_image.non_clear_pixel_count>0);
    auto dead=alive;dead.sequence=2;dead.frame=256;dead.frame_rate=0;dead.gait_sequence=0;dead.weapon_model_index=0;
    dead.effects=32;
    const auto terminal=host.remote_player(context(model,dead,dead,&alive,2,10.1,10.1));
    CHECK_FALSE(terminal.gait_sample);CHECK_FALSE(terminal.attachment_model_index);
    const std::array dead_intents{terminal};const auto dead_scene=scene_for(dead_intents,center);
    const auto dead_image=image(dead_scene);CHECK(dead_image.non_clear_pixel_count>0);
    CHECK(dead_image.color_signature!=alive_image.color_signature);
    api::GameClientHost animation_host{hl::make_half_life_client_module()};animation_host.reset({1,1,1});
    auto animated_old=alive;animated_old.sequence=2;animated_old.gait_sequence=0;animated_old.weapon_model_index=0;
    auto animated_current=animated_old;animated_current.frame=128;animated_current.animation_time_seconds=10.1;
    std::array<std::uint64_t,3> animation_signatures{};
    for(std::size_t step=0;step<3;++step) {
        const auto intent=animation_host.remote_player(context(model,animated_current,animated_current,&animated_old,
            2,10.1,10.0+double(step)*0.05));
        CHECK(intent.sample.frame_coordinate==Approx(double(step)*2.5));
        const std::array intents{intent};const auto animated_scene=scene_for(intents,center);
        const auto pixels=image(animated_scene);CHECK(pixels.non_clear_pixel_count>0);
        animation_signatures[step]=pixels.color_signature;
        CHECK(image(animated_scene).color_signature==pixels.color_signature);
    }
    CHECK(animation_signatures[0]!=animation_signatures[1]);
    CHECK(animation_signatures[1]!=animation_signatures[2]);
    auto corpse=dead;corpse.entity_number=40;corpse.player_movement_schema=false;corpse.render_effect=17;corpse.render_amount=2;
    auto c=context(model,corpse,corpse,nullptr,3,10.2,10.2);c.server_owned_proxy=true;
    const auto corpse_intent=host.remote_player(c);REQUIRE(corpse_intent.status==api::RemotePlayerPresentationStatus::ready);
    auto fresh=context(model,alive,alive,&dead,4,10.3,10.3);fresh.discontinuity=true;
    const auto living=host.remote_player(fresh);CHECK_FALSE(living.previous_sample);
    const std::array pair{corpse_intent,living};const std::array places{assets::AssetVector3{0,-25,0},assets::AssetVector3{0,25,0}};
    const auto pair_scene=scene_for(pair,places);const auto pair_image=image(pair_scene);
    auto baseline_scene=pair_scene;baseline_scene.dynamic_entities.reset();
    auto baseline_gl=std::make_unique<renderer::opengl::OpenGlRenderer>();
    baseline_gl->render(baseline_scene,{320,240});
    const auto baseline=baseline_gl->observe_framebuffer({320,240},baseline_scene.clear_color,true);REQUIRE(baseline);
    std::size_t left=0,right=0;
    for(std::size_t pixel=0;pixel<pair_image.rgba8.size()/4;++pixel)
        if(!std::equal(pair_image.rgba8.begin()+pixel*4,pair_image.rgba8.begin()+pixel*4+3,baseline.rgba8.begin()+pixel*4))
            (pixel%320<160 ? left : right)++;
    CHECK(left>0);CHECK(right>0); // Independent world positions, no between-life ghost draw.
    auto occluded=pair_scene;occluded.world_flash_count=1;
    occluded.world_flashes[0]={{0,-25,10},0.5F,{1,1,1,1}};
    CHECK(image(occluded).color_signature==pair_image.color_signature);
    occluded.world_flashes[0].center.x=60;
    CHECK(image(occluded).color_signature!=pair_image.color_signature);
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1);
    CHECK(image(pair_scene).color_signature==pair_image.color_signature);
    std::cout << "e13_owned_gl alive_pixels=" << alive_image.non_clear_pixel_count
        << " terminal_pixels=" << dead_image.non_clear_pixel_count << " corpse_respawn_left=" << left
        << " right=" << right << " uploads=1 live_state=false\n";
    baseline_gl.reset();gl->release_renderer();
}

TEST_CASE("Actual Half-Life host main transition and gait compose to independently expected bone palette",
    "[e10][game][remote-player][pose-handoff]") {
    const auto model=player_model();
    const auto before=player();
    api::GameClientHost host{hl::make_half_life_client_module()}; host.reset({1U,1U,1U});
    REQUIRE(host.remote_player(context(model,before,before)).status==api::RemotePlayerPresentationStatus::ready);
    auto after=before; after.origin.x=12.0; after.sequence=2U; after.animation_time_seconds=10.1;
    const auto started=host.remote_player(context(model,after,after,&before,2U,10.1,10.1));
    REQUIRE(started.previous_sample); REQUIRE(started.gait_sample);
    const auto intent=host.remote_player(context(model,after,after,&before,2U,10.1,10.2));
    REQUIRE(intent.previous_sample); REQUIRE(intent.gait_sample);
    CHECK(intent.previous_weight==Approx(0.5));
    const studio::StudioPoseModelIdentity identity{"models/project_owned_player.mdl",{19U,31U}};
    studio::StudioPoseEvaluator evaluator;
    const auto posed=evaluator.compose(identity,model,composition(intent));
    INFO((posed.error ? posed.error->context : "composed actual Half-Life intent"));
    REQUIRE(posed); REQUIRE(posed.pose);
    CHECK(posed.pose->statistics().composition_sample_count==3U);
    REQUIRE(posed.pose->local_bones().size()==8U);
    REQUIRE(posed.pose->world_bones().size()==8U);
    constexpr std::array<std::uint8_t,8U> lower{1U,1U,0U,0U,0U,0U,1U,1U};
    for(std::size_t i=0U;i<lower.size();++i) {
        const auto expected_x=lower[i] ? 100.0F+static_cast<float>(i) :
            20.0F+static_cast<float>(i)+2.0F*127.0F/255.0F;
        CHECK(posed.pose->local_bones()[i].translation.x==Approx(expected_x).margin(1e-4F));
        CHECK(posed.pose->local_bones()[i].translation.z==Approx(lower[i] ? 1.1F : 1.5F).margin(0.002F));
        for(const auto component:posed.pose->world_bones()[i].transform.values) CHECK(std::isfinite(component));
    }
    // Independent hierarchy expectations, not comparison to another evaluator.
    CHECK(posed.pose->world_bones()[0].transform.values[3]==Approx(100.0F));
    CHECK(posed.pose->world_bones()[1].transform.values[3]==Approx(201.0F));
    CHECK(posed.pose->world_bones()[2].transform.values[3]==Approx(223.0F+254.0F/255.0F).margin(1e-4F));
    CHECK(posed.pose->world_bones()[6].transform.values[3]==Approx(307.0F));
    CHECK(posed.pose->world_bones()[7].transform.values[3]==Approx(308.0F));
    REQUIRE(posed.pose->body_selection().bodyparts.size()==1U);
    CHECK(posed.pose->body_selection().bodyparts[0].submodel_index==0U);
    CHECK(posed.pose->skin_selection().texture_indices_by_skin_reference==std::vector<std::uint16_t>{0U});
    const auto bounds=evaluator.posed_bounds(identity,model,*posed.pose);
    REQUIRE(bounds);
    CHECK(bounds.bounds->minimum.x==Approx(100.0F));
    CHECK(bounds.bounds->maximum.x==Approx(308.0F));
    CHECK(host.drain_audio().count==0U);
    host.teardown();
}

TEST_CASE("E32 looping main raw clocks advance through buffered brackets and composed upper bones",
    "[e32-loop-clock][e32-remote-buffer][e10][game][remote-player][pose-handoff]") {
    const auto model=player_model();
    auto first=player();first.sequence=2U;first.weapon_model_index=21U;
    auto second=first;second.frame=51.2;second.animation_time_seconds=10.1;
    second.origin={12,0,8};second.gait_sequence=0U;second.weapon_model_index=22U;
    auto third=second;third.frame=102.4;third.animation_time_seconds=10.2;
    third.origin={24,0,0};third.gait_sequence=2U;third.weapon_model_index=21U;
    api::GameClientHost dense{hl::make_half_life_client_module()};dense.reset({1,1,1});
    api::GameClientHost sparse{hl::make_half_life_client_module()};sparse.reset({1,1,1});
    REQUIRE(dense.remote_player(context(model,first,first)).status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(sparse.remote_player(context(model,first,first)).status==api::RemotePlayerPresentationStatus::ready);
    studio::StudioPoseEvaluator evaluator;
    const studio::StudioPoseModelIdentity identity{"owned_loop_clock_upper_body",{19,31}};
    for(unsigned step=0;step<=20U;++step) {
        CAPTURE(step);
        const bool later=step>10U;
        const auto& previous=later ? second : first;
        const auto& current=later ? third : second;
        const double now=later ? 10.2 : 10.1,time=10.0+static_cast<double>(step)*.01;
        const auto& selected=step==10U || step==20U ? current : previous;
        auto sampled=selected;sampled.frame=200.0; // Never replace either committed raw clock.
        const auto input=context(model,sampled,current,&previous,later ? 3U : 2U,now,time);
        const auto result=dense.remote_player(input);
        REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
        const double expected=static_cast<double>(step)*.2;
        CHECK(result.sample.frame_coordinate==Approx(expected).margin(1e-6));
        CHECK(result.attachment_model_index==selected.weapon_model_index);
        CHECK(result.gait_sample.has_value()==(*selected.gait_sequence!=0U));
        if(result.gait_sample) CHECK(result.gait_sample->sequence==selected.gait_sequence);
        CHECK_FALSE(result.previous_sample);
        const auto repeat=dense.remote_player(input);
        CHECK(repeat.sample==result.sample);CHECK(repeat.gait_sample==result.gait_sample);
        const auto posed=evaluator.compose(identity,model,composition(result));
        REQUIRE(posed);REQUIRE(posed.pose);
        // Bone2 is in the upper body; both owned blends encode z(frame)=frame.
        CHECK(posed.pose->local_bones()[2].translation.z==Approx(expected).margin(1e-5));
        if(step==13U || step==20U) {
            // Skip the entire preceding bracket: raw main phase is not render history.
            const auto skipped=sparse.remote_player(input);
            REQUIRE(skipped.status==api::RemotePlayerPresentationStatus::ready);
            CHECK(skipped.sample.frame_coordinate==Approx(expected).margin(1e-6));
            CHECK(skipped.sample==result.sample);
        }
    }
    dense.teardown();sparse.teardown();
}

TEST_CASE("E32 delayed looping raw clocks retain forward reverse wrap pause and rate changes",
    "[e32-loop-clock][e10][game][remote-player]") {
    const auto model=player_model();
    struct ClockCase {double old_frame,new_frame,old_rate,new_rate;std::array<double,4> expected;double after;};
    const std::array cases{
        ClockCase{243.2,38.4,1,1,{9.7,.3,1.1,1.5},2.0},
        ClockCase{12.8,217.6,-1,-1,{.3,9.7,8.9,8.5},8.0},
        ClockCase{102.4,102.4,0,1,{4,4,4,4},4.5},
        ClockCase{76.8,102.4,.5,-1,{3.1,3.4,3.8,4},3.5}};
    constexpr std::array times{10.01,10.04,10.08,10.1};
    for(std::size_t case_index=0;case_index<cases.size();++case_index) {
        CAPTURE(case_index);
        const auto& selected=cases[case_index];
        auto previous=player();previous.sequence=2U;previous.frame=selected.old_frame;previous.frame_rate=selected.old_rate;
        auto current=previous;current.frame=selected.new_frame;current.frame_rate=selected.new_rate;
        current.animation_time_seconds=10.1;
        hl::HalfLifeRemotePlayerPresentation presentation;seed(presentation,model,previous);
        for(std::size_t i=0;i<times.size();++i) {
            CAPTURE(i);
            auto sampled=previous;sampled.frame=200.0;sampled.frame_rate=0.0;
            const auto result=presentation.sample(context(model,sampled,current,&previous,2U,10.1,times[i]));
            REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
            CHECK(result.sample.frame_coordinate==Approx(selected.expected[i]).margin(1e-6));
        }
        CHECK(presentation.sample(context(model,current,current,&previous,2U,10.1,10.125)).sample.frame_coordinate==
            Approx(selected.after).margin(1e-6));
    }
}

TEST_CASE("E32 looping clock corrections and restarts remain discrete at their raw animation time",
    "[e32-loop-clock][e10][game][remote-player]") {
    const auto model=player_model();
    auto previous=player();previous.sequence=2U;previous.frame=128.0;
    auto current=previous;current.frame=0.0;current.animation_time_seconds=10.08;
    hl::HalfLifeRemotePlayerPresentation presentation;seed(presentation,model,previous);
    constexpr std::array times{10.04,10.079,10.08,10.09,10.1};
    constexpr std::array expected{5.8,6.58,0.0,.2,.4};
    for(std::size_t i=0;i<times.size();++i) {
        CAPTURE(i);
        const auto input=context(model,previous,current,&previous,2U,10.1,times[i]);
        const auto result=presentation.sample(input);
        REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(result.sample.frame_coordinate==Approx(expected[i]).margin(1e-6));
        CHECK_FALSE(result.previous_sample); // A same-sequence restart is not a blended transition.
        CHECK(presentation.sample(input).sample==result.sample);
    }
    // Equal/regressing raw timestamps are corrections, not a new delayed clock.
    for(const double animation_time:std::array{10.0,9.9}) {
        hl::HalfLifeRemotePlayerPresentation correction;
        seed(correction,model,previous);current.frame=51.2;current.animation_time_seconds=animation_time;
        auto sampled=previous;sampled.frame=200.0;sampled.animation_time_seconds=10.05;
        const auto result=correction.sample(context(model,sampled,current,&previous,2U,10.1,10.05));
        REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(result.sample.frame_coordinate==Approx(animation_time==10.0 ? 3.0 : 5.0).margin(1e-6));
    }
}

TEST_CASE("E32 delayed looping clock cannot bridge continuity boundaries on first or repeated samples",
    "[e32-loop-clock][e10][game][remote-player]") {
    const auto model=player_model();
    for(unsigned boundary=0;boundary<13U;++boundary) {
        CAPTURE(boundary);
        auto previous=player();previous.sequence=2U;
        auto current=previous;current.frame=51.2;current.animation_time_seconds=10.1;
        hl::HalfLifeRemotePlayerPresentation presentation;seed(presentation,model,previous);
        auto input=context(model,current,current,&previous,2U,10.1,10.05);
        switch(boundary) {
        case 0:input.discontinuity=true;break;
        case 1:current.effects=32U;break;
        case 2:previous.effects=32U;break;
        case 3:current.origin.x=200.0;break;
        case 4:input.entity_lifetime=2U;break;
        case 5:input.model_resource_id=28U;break;
        case 6:input.model_revision.primary=20U;break;
        case 7:input.network_generation=2U;break;
        case 8:input.map_generation=2U;break;
        case 9:input.previous_server_seconds=9.0;break;
        case 10:input.previous=nullptr;input.previous_record_identity=0U;input.previous_record_ordinal=0U;break;
        case 11:input.previous_server_seconds=10.1;break;
        case 12:previous.model_index=8U;break;
        }
        for(unsigned repeat=0;repeat<2U;++repeat) {
            CAPTURE(repeat);
            const auto result=presentation.sample(input);
            REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
            CHECK(result.sample.frame_coordinate==Approx(2.0));
        }
    }
}

TEST_CASE("E12 committed fire leaves server attack gait transition and evaluated body pose authoritative",
          "[e12][remote-player][pose-handoff][game-module]") {
    const auto model=player_model();const auto before=player();
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    host.set_movement_audio_time_origin(0);
    REQUIRE(host.remote_player(context(model,before,before)).status==api::RemotePlayerPresentationStatus::ready);
    auto attack=before;attack.sequence=2;attack.animation_time_seconds=10.1;attack.weapon_model_index=21;
    REQUIRE(host.remote_player(context(model,attack,attack,&before,2,10.1,10.1)).previous_sample);
    const auto first=host.remote_player(context(model,attack,attack,&before,2,10.1,10.2));
    api::ScriptedEventBinding binding;binding.index=71;
    constexpr char name[]="events/glock1.sc";std::copy_n(name,sizeof(name),binding.name.begin());
    host.configure_scripted_events(std::span{&binding,1});
    api::CommittedScriptedEvent event;event.generation=1;event.record=2;event.record_ordinal=2;
    event.emitter_entity=2;event.event_index=71;event.received_at_seconds=10.2;
    host.committed_scripted_event(event);REQUIRE(host.drain_remote_effects(10.2).count==1);
    const auto after=host.remote_player(context(model,attack,attack,&before,2,10.1,10.2));
    CHECK(after.sample.sequence==2);CHECK(after.sample.sequence==first.sample.sequence);
    CHECK(after.sample.frame_coordinate==first.sample.frame_coordinate);
    REQUIRE(after.gait_sample);REQUIRE(first.gait_sample);
    CHECK(after.gait_sample->sequence==first.gait_sample->sequence);
    CHECK(after.gait_sample->frame_coordinate==first.gait_sample->frame_coordinate);
    CHECK(after.previous_weight==first.previous_weight);CHECK(after.gait_bone_mask==first.gait_bone_mask);
    const studio::StudioPoseModelIdentity identity{"project_owned/player",{19,31}};
    studio::StudioPoseEvaluator evaluator;
    const auto p=evaluator.compose(identity,model,composition(first));
    const auto q=evaluator.compose(identity,model,composition(after));REQUIRE(p);REQUIRE(q);
    REQUIRE(p.pose->world_bones().size()==q.pose->world_bones().size());
    for(std::size_t i=0;i<p.pose->world_bones().size();++i)
        CHECK(p.pose->world_bones()[i].transform.values==q.pose->world_bones()[i].transform.values);
    host.teardown();
}

TEST_CASE("E11 Half-Life module chooses only the sampled committed attachment slot",
          "[e11][game][remote-player]") {
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    const auto model=player_model();auto sampled=player(),current=player();
    sampled.weapon_model_index=73U;current.weapon_model_index=92U;
    auto intent=host.remote_player(context(model,sampled,current));
    REQUIRE(intent.status==api::RemotePlayerPresentationStatus::ready);
    CHECK(intent.attachment_model_index==73U); // Never lead the body interpolation timeline.
    sampled.weapon_model_index=0U;
    CHECK_FALSE(host.remote_player(context(model,sampled,current)).attachment_model_index);
    sampled.weapon_model_index.reset();
    CHECK_FALSE(host.remote_player(context(model,sampled,current)).attachment_model_index);
    sampled.entity_number=1U;sampled.weapon_model_index=73U;
    CHECK_FALSE(host.remote_player(context(model,sampled,sampled)).attachment_model_index);
    host.teardown();CHECK_FALSE(host.remote_player(context(model,current,current)).attachment_model_index);
}
TEST_CASE("Selected Half-Life module returns numeric remote player policy and actual pose intents",
    "[e10][game][remote-player]") {
    api::GameClientHost host{hl::make_half_life_client_module()};
    host.reset({1U,1U,1U});
    const auto model=player_model();
    const auto entity=player();
    const auto policy=host.remote_player_policy();
    CHECK(policy.enabled);
    CHECK(policy.maximum_players==32U);
    CHECK(policy.interpolation_delay_seconds==Approx(0.1));
    CHECK(policy.maximum_observation_gap_seconds==Approx(0.25));
    CHECK(policy.teleport_distance_units==Approx(128.0));
    CHECK(policy.no_interpolation_effect_mask==32U);
    const auto intent=host.remote_player(context(model,entity,entity));
    REQUIRE(intent.status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(intent.gait_sample);
    CHECK(intent.sample.sequence==0U);
    CHECK(intent.gait_sample->sequence==1U);
    CHECK(intent.bone_count==8U);
    constexpr std::array<std::uint8_t,8U> expected{1,1,0,0,0,0,1,1};
    for(std::size_t i=0;i<expected.size();++i) CHECK(intent.gait_bone_mask[i]==expected[i]);
    CHECK(host.drain_audio().count==0U);
    host.teardown();
    CHECK_FALSE(host.remote_player_policy().enabled);
    CHECK(host.remote_player(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::silent);
}

TEST_CASE("Remote gait covers forward backwards strafe and diagonal without rotating aim",
    "[e10][game][remote-player]") {
    struct Direction {double x,y,yaw,frame; std::uint8_t torso;};
    const std::array directions{
        Direction{12,0,0,1.1,127},Direction{-12,0,0,9.9,127},
        Direction{0,12,90,1.1,31},Direction{0,-12,-90,1.1,223},
        Direction{12,12,45,std::sqrt(288.0)*11.0/120.0,79},
        Direction{-12,12,-45,11.0-std::sqrt(288.0)*11.0/120.0,175}};
    const auto model=player_model();
    const auto before=player();
    for(const auto& direction:directions) {
        CAPTURE(direction.x,direction.y);
        hl::HalfLifeRemotePlayerPresentation presentation;
        seed(presentation,model,before);
        auto after=before;
        after.origin={direction.x,direction.y,0.0};
        const auto output=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
        REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
        REQUIRE(output.gait_sample);
        CHECK(output.transform_angles[0]==Approx(0.0));
        CHECK(output.transform_angles[1]==Approx(direction.yaw));
        CHECK(output.gait_sample->frame_coordinate==Approx(direction.frame));
        for(const auto control:output.sample.controllers) CHECK(control==direction.torso);
        // Body root + distributed torso compensate the original zero view yaw.
        const auto torso_degrees=static_cast<double>(output.sample.controllers[0])*60.0/255.0-30.0;
        CHECK(std::abs(std::remainder(output.transform_angles[1]+4.0*torso_degrees,360.0))<1.0);
    }
}

TEST_CASE("Remote pitch uses imported blend range and preserves out of range residual",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    struct Aim {double wire_pitch,residual; std::uint8_t blend;};
    for(const auto aim:std::array{Aim{0,0,127},Aim{15,0,255},Aim{345,0,0},
        Aim{30,15,255},Aim{330,-15,0},Aim{9,0,204}}) {
        hl::HalfLifeRemotePlayerPresentation presentation;
        auto entity=player(); entity.angles.x=aim.wire_pitch; entity.angles.y=359.0;
        const auto output=presentation.sample(context(model,entity,entity));
        REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(output.sample.blending[0]==aim.blend);
        CHECK(output.transform_angles[0]==Approx(aim.residual));
        CHECK(output.transform_angles[1]==Approx(-1.0));
    }
}

TEST_CASE("E13 delayed nonloop death frame follows committed endpoints without packet steps",
    "[e13-animation][e13][game][remote-player]") {
    auto model=player_model();model.sequences[2].source_flags=0U;
    auto old=player();old.sequence=2U;old.gait_sequence=0U;old.weapon_model_index=0U;
    old.frame=0.0;old.animation_time_seconds=10.0;
    auto current=old;current.frame=51.2;current.animation_time_seconds=10.1;
    hl::HalfLifeRemotePlayerPresentation presentation;
    double last=-1;
    for(std::size_t step=0;step<=10;++step) {
        const double time=10.0+double(step)*0.01;
        const auto result=presentation.sample(context(model,current,current,&old,2U,10.1,time));
        REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(result.sample.frame_coordinate==Approx(double(step)*0.2));
        CHECK(result.sample.frame_coordinate>=last);last=result.sample.frame_coordinate;
        CHECK_FALSE(result.gait_sample);CHECK_FALSE(result.attachment_model_index);
        const auto pose=studio::StudioPoseEvaluator{}.compose({"owned_delayed_death",{19,31}},model,composition(result));
        REQUIRE(pose);CHECK(pose.pose->world_bones()[0].transform.values[11]==Approx(double(step)*0.2));
    }
    // A respawn/no-interp boundary must not smooth the old death into a new pose.
    auto boundary=context(model,current,current,&old,3U,10.1,10.05);boundary.discontinuity=true;
    const auto reset=presentation.sample(boundary);CHECK(reset.sample.frame_coordinate==Approx(2.0));
    for(unsigned mode=0;mode<5;++mode) {
        hl::HalfLifeRemotePlayerPresentation separate;
        auto previous=old;auto newest=current;
        auto blocked=context(model,newest,newest,&previous,2U,10.1,10.05);
        if(mode==0) newest.effects=32U;
        if(mode==1) newest.origin.x=256.0;
        if(mode==2) previous.sequence=0U;
        if(mode==3) previous.frame=102.4; // explicit same-sequence rewind/restart
        if(mode==4) blocked.previous_server_seconds=9.0; // observation gap
        CHECK(separate.sample(blocked).sample.frame_coordinate==Approx(2.0));
    }
}

TEST_CASE("Remote main clock retains wire frame rate pause and nonloop clamping",
    "[e10][game][remote-player]") {
    auto model=player_model();
    model.sequences[0].source_flags=0U;
    for(const auto rate:std::array{0.0,0.5,1.0,-1.0}) {
        hl::HalfLifeRemotePlayerPresentation presentation;
        auto entity=player(); entity.frame=128.0; entity.frame_rate=rate;
        const auto output=presentation.sample(context(model,entity,entity,nullptr,1U,10.1,10.1));
        REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(output.sample.frame_coordinate==Approx(5.0+rate));
    }
    hl::HalfLifeRemotePlayerPresentation presentation;
    auto entity=player(); entity.frame=256.0;
    CHECK(presentation.sample(context(model,entity,entity,nullptr,1U,10.1,10.1)).sample.frame_coordinate==Approx(10.0));
}

TEST_CASE("Remote player consumes sampled internal blend without replacing committed animation clock",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    auto current=player(); current.frame=128.0; current.blending[1]=240U;
    auto sampled=current; sampled.frame=0.0; sampled.blending[1]=80U;
    sampled.controllers={0U,64U,192U,255U};
    hl::HalfLifeRemotePlayerPresentation presentation;
    const auto output=presentation.sample(context(model,sampled,current,nullptr,1U,10.1,10.1));
    REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
    CHECK(output.sample.frame_coordinate==Approx(6.0));
    CHECK(output.sample.blending[0]==127U);
    CHECK(output.sample.blending[1]==80U);
    // In the supported HL skeleton all four are torso controls: the game
    // deliberately replaces sampled controls with its view/gait compensation.
    for(const auto controller:output.sample.controllers) CHECK(controller==127U);
    sampled.blending[1]=160U;
    CHECK(presentation.sample(context(model,sampled,current,nullptr,1U,10.1,10.1)).sample.blending[1]==160U);
    sampled.controllers[1].reset();
    CHECK(presentation.sample(context(model,sampled,current,nullptr,1U,10.1,10.1)).status==
        api::RemotePlayerPresentationStatus::missing_fields);
    sampled=current; sampled.blending[1].reset();
    CHECK(presentation.sample(context(model,sampled,current,nullptr,1U,10.1,10.1)).status==
        api::RemotePlayerPresentationStatus::missing_fields);
}

TEST_CASE("Half-Life host keeps sampled discrete appearance at alpha zero half and one",
    "[e10][game][remote-player][pose-handoff]") {
    auto model=player_model();
    model.textures.push_back(model.textures[0]);
    model.skin_families.push_back({{1U}});
    model.submodels.push_back(model.submodels[0]);
    model.bodyparts[0].submodel_indices.push_back(1U);
    const auto before=player();
    auto current=before;
    current.sequence=2U; current.animation_time_seconds=10.1;
    current.body=1U; current.skin=1;
    api::GameClientHost host{hl::make_half_life_client_module()}; host.reset({1U,1U,1U});
    REQUIRE(host.remote_player(context(model,before,before)).status==api::RemotePlayerPresentationStatus::ready);
    const studio::StudioPoseModelIdentity identity{"models/project_owned_appearance.mdl",{19U,31U}};
    for(const auto alpha:std::array{0.0,0.5,1.0}) {
        CAPTURE(alpha);
        // Independent discrete endpoint input from the neutral interpolator:
        // old appearance until the new endpoint, while its clock stays current.
        auto sampled=current;
        sampled.body=alpha<1.0 ? 0U : 1U; sampled.skin=alpha<1.0 ? 0 : 1;
        const auto intent=host.remote_player(context(model,sampled,current,&before,
            2U,10.1,10.0+0.1*alpha));
        REQUIRE(intent.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(intent.sample.sequence==2U);
        CHECK(intent.sample.body==(alpha<1.0 ? 0 : 1));
        CHECK(intent.sample.skin==(alpha<1.0 ? 0U : 1U));
        REQUIRE(intent.gait_sample);
        CHECK(intent.gait_sample->body==intent.sample.body);
        CHECK(intent.gait_sample->skin==intent.sample.skin);
        REQUIRE(intent.previous_sample);
        CHECK(intent.previous_sample->body==0);
        CHECK(intent.previous_sample->skin==0U);
        const auto posed=studio::StudioPoseEvaluator{}.compose(identity,model,composition(intent));
        INFO((posed.error ? posed.error->context : "sampled appearance composed"));
        REQUIRE(posed); REQUIRE(posed.pose);
        REQUIRE(posed.pose->body_selection().bodyparts.size()==1U);
        CHECK(posed.pose->body_selection().bodyparts[0].submodel_index==(alpha<1.0 ? 0U : 1U));
        CHECK(posed.pose->skin_selection().texture_indices_by_skin_reference==
            std::vector<std::uint16_t>{static_cast<std::uint16_t>(alpha<1.0 ? 0U : 1U)});
    }
    auto invalid=current; invalid.skin=2;
    CHECK(host.remote_player(context(model,invalid,current,&before,2U,10.1,10.1)).status==
        api::RemotePlayerPresentationStatus::missing_fields);
    invalid=current; invalid.body.reset();
    CHECK(host.remote_player(context(model,invalid,current,&before,2U,10.1,10.1)).status==
        api::RemotePlayerPresentationStatus::missing_fields);
    host.teardown();
}

TEST_CASE("Remote gait final fractional reference interval selects frame zero",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    const auto before=player();
    hl::HalfLifeRemotePlayerPresentation presentation;
    seed(presentation,model,before);
    auto after=before; after.origin.x=120.0*10.5/11.0;
    const auto output=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
    REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(output.gait_sample);
    CHECK(output.gait_sample->frame_coordinate==Approx(0.0));
    CHECK(presentation.sample(context(model,after,after,&before,2U,10.1,10.1)).gait_sample==output.gait_sample);
}

TEST_CASE("Remote gait phase samples anchors analytically and does not depend on render cadence",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    const auto before=player();
    auto after=before; after.origin.x=12.0;
    hl::HalfLifeRemotePlayerPresentation sparse,dense;
    seed(sparse,model,before); seed(dense,model,before);
    auto half=after; half.origin.x=6.0;
    const auto halfway=sparse.sample(context(model,half,after,&before,2U,10.1,10.05));
    REQUIRE(halfway.gait_sample);
    CHECK(halfway.gait_sample->frame_coordinate==Approx(0.55));
    for(int i=0;i<=120;++i) {
        auto sampled=after; sampled.origin.x=12.0*static_cast<double>(i)/120.0;
        const auto output=dense.sample(context(model,sampled,after,&before,2U,10.1,10.0+0.1*i/120.0));
        REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
    }
    const auto sparse_end=sparse.sample(context(model,after,after,&before,2U,10.1,10.1));
    const auto dense_end=dense.sample(context(model,after,after,&before,2U,10.1,10.1));
    REQUIRE(sparse_end.gait_sample); REQUIRE(dense_end.gait_sample);
    CHECK(*sparse_end.gait_sample==*dense_end.gait_sample);
    CHECK(sparse_end.sample==dense_end.sample);
    const auto repeated=dense.sample(context(model,after,after,&before,2U,10.1,10.1));
    CHECK(*repeated.gait_sample==*dense_end.gait_sample);
    CHECK(repeated.sample==dense_end.sample);
}

TEST_CASE("E32 railing run jump pose changes obey the delayed sampled endpoint",
    "[e32-remote-buffer][rail-regression][e10][game][remote-player]") {
    const auto model=player_model();const auto before=player();
    hl::HalfLifeRemotePlayerPresentation presentation;seed(presentation,model,before);
    auto after=before;after.origin={12,0,8};after.sequence=2;after.gait_sequence=2;
    after.animation_time_seconds=10.1;
    auto halfway=before;halfway.origin={6,0,4};
    const auto delayed=presentation.sample(context(model,halfway,after,&before,2,10.1,10.05));
    REQUIRE(delayed.status==api::RemotePlayerPresentationStatus::ready);
    CHECK(delayed.sample.sequence==before.sequence);REQUIRE(delayed.gait_sample);
    CHECK(delayed.gait_sample->sequence==before.gait_sequence);
    CHECK(delayed.gait_sample->frame_coordinate==Approx(std::sqrt(208.0)*11.0/120.0*.5));
    CHECK_FALSE(delayed.previous_sample);
    CHECK(presentation.sample(context(model,halfway,after,&before,2,10.1,10.05)).gait_sample==delayed.gait_sample);
    const auto endpoint=presentation.sample(context(model,after,after,&before,2,10.1,10.1));
    CHECK(endpoint.sample.sequence==after.sequence);REQUIRE(endpoint.gait_sample);
    CHECK(endpoint.gait_sample->sequence==after.gait_sequence);CHECK(endpoint.previous_sample);
}

TEST_CASE("E32 railing turn gait yaw is sampled between committed directions instead of stepping on receipt",
    "[e32-remote-buffer][rail-regression][e10][game][remote-player]") {
    const auto model=player_model();const auto before=player();
    hl::HalfLifeRemotePlayerPresentation presentation;seed(presentation,model,before);
    auto after=before;after.origin.y=12;
    for(int step=0;step<=10;++step) {
        auto posed=before;posed.origin.y=1.2*step;
        const auto result=presentation.sample(context(model,posed,after,&before,2,10.1,10+.01*step));
        REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(result.gait_yaw_degrees==Approx(9.0*step).margin(.000001));
        CHECK(result.transform_angles[1]==Approx(9.0*step).margin(.000001));
    }
    auto opposite=after;opposite.origin.y=0;
    for(int step=0;step<=10;++step) {
        auto posed=after;posed.origin.y=12-1.2*step;
        const auto result=presentation.sample(context(model,posed,opposite,&after,3,10.2,10.1+.01*step));
        REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(result.gait_yaw_degrees==Approx(90-18.0*step).margin(.000001));
        CHECK(result.transform_angles[1]==Approx(90-18.0*step).margin(.000001));
    }
}

TEST_CASE("E32 buffered railing run airborne changes compose through the actual Half Life host",
    "[e32-remote-buffer][rail-regression][e10][game][remote-player][pose-handoff]") {
    namespace g=hlclient::goldsrc;
    using State=client::RuntimeClientObservationState;
    const auto model=player_model();
    api::GameClientHost host{hl::make_half_life_client_module()};host.reset({1,1,1});
    g::RuntimePlayerSnapshotPresentation buffer;
    const auto observed=[](std::size_t ordinal,double time) {
        auto value=std::make_shared<State>();value->generation=1;value->publication_revision=ordinal;
        value->server_time_seconds=time;
        const client::RuntimeSubstateMetadata metadata{1,client::RuntimeObservationFreshness::observed_in_record,
            client::RuntimeObservationCompleteness::complete_reconstruction,
            client::RuntimeObservationSource{ordinal,ordinal,1,0,8}};
        value->entity_metadata=value->server_time_metadata=metadata;value->client_metadata.generation=1;
        auto entity=player();entity.weapon_model_index=21;
        entity.origin={320*(time-10),0,48+8*static_cast<double>(ordinal%3)};
        entity.sequence=ordinal%4==0 ? 2U : 0U;entity.gait_sequence=ordinal%4==0 ? 0U : 1U;
        entity.animation_time_seconds=time;
        value->packet_entities.push_back(entity);
        value->canonical_state_hash=client::runtime_observation_canonical_hash(*value);
        REQUIRE(client::valid_runtime_observation(*value));return value;
    };
    auto current=observed(2,10.04);buffer.observe(observed(1,10));buffer.observe(current);
    g::RuntimeEntityInterpolationOptions policy;policy.limits.maximum_snapshot_gap_seconds=.25;
    policy.teleport_distance=128;policy.no_interpolation_effect_mask=32;
    constexpr std::array<std::uint32_t,1> selected{2};std::size_t next=3;
    double last_x=-1;
    for(std::size_t frame=0;frame<180;++frame) {
        const double app=100+.005*static_cast<double>(frame);
        if(next<24 && app>=100+.04*static_cast<double>(next-2)) {
            current=observed(next,10+.04*static_cast<double>(next-1));++next;buffer.observe(current);
        }
        const auto immutable=*current;
        const auto mixed=buffer.sample(*current,app,policy,.1,selected);REQUIRE(mixed);REQUIRE(mixed.players.size()==1);
        const auto& witness=mixed.players[0];const auto& posed=mixed.presentation.state->packet_entities[0];
        const auto& raw=witness.current->packet_entities[0];const auto& before=witness.previous->packet_entities[0];
        auto input=context(model,posed,raw,&before,witness.current->entity_metadata.source->record_ordinal,
            *witness.current->server_time_seconds,witness.sample_seconds);
        input.previous_record_identity=witness.previous->entity_metadata.source->record_identity;
        input.previous_record_ordinal=witness.previous->entity_metadata.source->record_ordinal;
        input.previous_server_seconds=*witness.previous->server_time_seconds;input.discontinuity=witness.discontinuity;
        const auto intent=host.remote_player(input);REQUIRE(intent.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(intent.sample.sequence==posed.sequence);
        CHECK(intent.gait_sample.has_value()==(*posed.gait_sequence!=0));
        if(intent.gait_sample)CHECK(intent.gait_sample->sequence==posed.gait_sequence);
        const studio::StudioPoseModelIdentity identity{"models/project_owned_player.mdl",{19,31}};
        const auto palette=studio::StudioPoseEvaluator{}.compose(identity,model,composition(intent));REQUIRE(palette);
        for(const auto& bone:palette.pose->world_bones())for(const auto value:bone.transform.values)CHECK(std::isfinite(value));
        CHECK(*posed.origin.x>=last_x);CHECK(*current==immutable);last_x=*posed.origin.x;
    }
}

TEST_CASE("Stationary remote gait eases yaw analytically and uses zero linear motion FPS",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    auto before=player(); before.gait_sequence=2U;
    hl::HalfLifeRemotePlayerPresentation presentation;
    seed(presentation,model,before);
    auto after=before; after.angles.y=90.0;
    const auto output=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
    REQUIRE(output.gait_sample);
    CHECK(output.transform_angles[1]==Approx(90.0*(1.0-std::exp(-0.4))));
    CHECK(output.gait_sample->frame_coordinate==Approx(2.0));
    const auto repeat=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
    CHECK(repeat.transform_angles==output.transform_angles);
    CHECK(repeat.gait_sample==output.gait_sample);
}

TEST_CASE("Remote crouch gait changes retain numeric phase across different frame counts",
    "[e10][game][remote-player][crouch-regression]") {
    auto model=player_model();
    auto short_gait=model.sequences[1];
    short_gait.label="project_short_crouch_gait";
    short_gait.frame_count=7U;
    short_gait.linear_movement.x=84.0F;
    for(auto& blend:short_gait.animation_blends) {
        for(auto& track:blend.bone_tracks) {
            for(auto& channel:track.channels) {
                channel.frame_coverage=7U;
                for(auto& run:channel.runs) {
                    run.total_frame_count=7U;
                    run.valid_value_count=7U;
                    run.quantized_values.resize(7U);
                }
            }
        }
    }
    model.sequences.push_back(std::move(short_gait));
    const auto before=player();
    hl::HalfLifeRemotePlayerPresentation presentation;
    seed(presentation,model,before);
    auto walking=before; walking.origin.x=12.0;
    const auto walked=presentation.sample(context(model,walking,walking,&before,2U,10.1,10.1));
    REQUIRE(walked.gait_sample);
    CHECK(walked.gait_sample->frame_coordinate==Approx(1.1));

    auto crouching=walking; crouching.origin.x=24.0; crouching.gait_sequence=3U;
    const auto crouched=presentation.sample(context(model,crouching,crouching,&walking,3U,10.2,10.2));
    REQUIRE(crouched.status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(crouched.gait_sample);
    CHECK(crouched.gait_sample->sequence==3U);
    // Retain1.1 frames, then add12/84*7=1 frame from the selected gait.
    CHECK(crouched.gait_sample->frame_coordinate==Approx(2.1));
    CHECK(presentation.sample(context(model,crouching,crouching,&walking,3U,10.2,10.2)).gait_sample==
        crouched.gait_sample);

    auto resumed=crouching; resumed.origin.x=36.0; resumed.gait_sequence=1U;
    const auto back=presentation.sample(context(model,resumed,resumed,&crouching,4U,10.3,10.3));
    REQUIRE(back.gait_sample);
    CHECK(back.gait_sample->sequence==1U);
    CHECK(back.gait_sample->frame_coordinate==Approx(3.2));

    auto idle=resumed; idle.gait_sequence=2U;
    const auto held=presentation.sample(context(model,idle,idle,&resumed,5U,10.4,10.4));
    REQUIRE(held.gait_sample);
    // A zero-linear-motion crouch-idle gait uses FPS and keeps the same phase.
    CHECK(held.gait_sample->sequence==2U);
    CHECK(held.gait_sample->frame_coordinate==Approx(5.2));
}

TEST_CASE("Equal time remote sources preserve crouch phase and an active main transition",
    "[e10][game][remote-player][crouch-regression]") {
    const auto model=player_model();
    const auto before=player();
    hl::HalfLifeRemotePlayerPresentation presentation;
    seed(presentation,model,before);
    auto after=before; after.origin.x=12.0; after.sequence=2U; after.animation_time_seconds=10.1;
    const auto started=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
    REQUIRE(started.gait_sample);
    REQUIRE(started.previous_sample);
    CHECK(started.gait_sample->frame_coordinate==Approx(1.1));
    CHECK(started.transition_identity==2U);

    auto equal_time=context(model,after,after,&after,3U,10.1,10.2);
    equal_time.previous_server_seconds=10.1;
    const auto held=presentation.sample(equal_time);
    REQUIRE(held.status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(held.gait_sample);
    REQUIRE(held.previous_sample);
    CHECK(held.gait_sample->frame_coordinate==Approx(1.1));
    CHECK(held.previous_sample==started.previous_sample);
    CHECK(held.transition_identity==2U);
    CHECK(held.previous_weight==Approx(0.5));
    const auto repeated=presentation.sample(equal_time);
    CHECK(repeated.gait_sample==held.gait_sample);
    CHECK(repeated.previous_sample==held.previous_sample);
    CHECK(repeated.previous_weight==held.previous_weight);

    auto next=after; next.origin.x=24.0;
    auto next_input=context(model,next,next,&after,4U,10.2,10.2);
    next_input.previous_server_seconds=10.1;
    const auto resumed=presentation.sample(next_input);
    REQUIRE(resumed.gait_sample);
    REQUIRE(resumed.previous_sample);
    CHECK(resumed.gait_sample->frame_coordinate==Approx(2.2));
    CHECK(resumed.transition_identity==2U);
    CHECK(resumed.previous_weight==Approx(0.5));
}

TEST_CASE("A remote non gait activity preserves the previous active crouch phase",
    "[e10][game][remote-player][crouch-regression]") {
    auto model=player_model();
    // Sequence0 is not the prior gait. Its unrelated single-frame duration
    // must not normalize the retained gait clock while layering is disabled.
    model.sequences[0].frame_count=1U;
    for(auto& blend:model.sequences[0].animation_blends) {
        for(auto& track:blend.bone_tracks) {
            for(auto& channel:track.channels) {
                channel.frame_coverage=1U;
                for(auto& run:channel.runs) {
                    run.total_frame_count=1U;
                    run.valid_value_count=1U;
                    run.quantized_values.resize(1U);
                }
            }
        }
    }
    auto first=player(); first.sequence=2U;
    hl::HalfLifeRemotePlayerPresentation presentation;
    seed(presentation,model,first);
    auto moved=first; moved.origin.x=60.0;
    const auto active=presentation.sample(context(model,moved,moved,&first,2U,10.1,10.1));
    REQUIRE(active.gait_sample);
    CHECK(active.gait_sample->frame_coordinate==Approx(5.5));
    auto non_gait=moved; non_gait.gait_sequence=0U;
    const auto paused=presentation.sample(context(model,non_gait,non_gait,&moved,3U,10.2,10.2));
    REQUIRE(paused.status==api::RemotePlayerPresentationStatus::ready);
    CHECK_FALSE(paused.gait_sample);
    CHECK(presentation.sample(context(model,non_gait,non_gait,&moved,3U,10.2,10.2)).sample==paused.sample);
    auto resumed=non_gait; resumed.origin.x=72.0; resumed.gait_sequence=1U;
    const auto restored=presentation.sample(context(model,resumed,resumed,&non_gait,4U,10.3,10.3));
    REQUIRE(restored.status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(restored.gait_sample);
    CHECK(restored.gait_sample->sequence==1U);
    // The active phase5.5 is paused, then advances12/120*11=1.1 frames.
    CHECK(restored.gait_sample->frame_coordinate==Approx(6.6));
    CHECK(presentation.sample(context(model,resumed,resumed,&non_gait,4U,10.3,10.3)).gait_sample==
        restored.gait_sample);
}

TEST_CASE("Remote crouch phase catches up bounded committed sources skipped by rendering",
    "[e10][game][remote-player][crouch-regression]") {
    const auto model=player_model();
    for(const bool fps_driven:std::array{false,true}) {
        CAPTURE(fps_driven);
        auto first=player();
        if(fps_driven) first.gait_sequence=2U;
        auto second=first; second.origin.x=fps_driven ? 0.0 : 12.0;
        auto third=second; third.origin.x=fps_driven ? 0.0 : 24.0;
        auto fourth=third; fourth.origin.x=fps_driven ? 0.0 : 36.0;
        hl::HalfLifeRemotePlayerPresentation dense,sparse;
        seed(dense,model,first); seed(sparse,model,first);
        REQUIRE(dense.sample(context(model,second,second,&first,2U,10.1,10.1)).gait_sample);
        REQUIRE(sparse.sample(context(model,second,second,&first,2U,10.1,10.1)).gait_sample);
        REQUIRE(dense.sample(context(model,third,third,&second,3U,10.2,10.2)).gait_sample);
        // The engine's latest exact pair is3->4 even though the last rendered
        // source was2. Source3 is authoritative, not a synthetic render frame.
        const auto sparse_input=context(model,fourth,fourth,&third,4U,10.3,10.3);
        const auto caught_up=sparse.sample(sparse_input);
        const auto every_source=dense.sample(sparse_input);
        REQUIRE(caught_up.status==api::RemotePlayerPresentationStatus::ready);
        REQUIRE(caught_up.gait_sample);
        REQUIRE(every_source.gait_sample);
        // Independent physical/time expectations:36/120*11, or20FPS*0.3s.
        CHECK(caught_up.gait_sample->frame_coordinate==Approx(fps_driven ? 6.0 : 3.3));
        CHECK(every_source.gait_sample->frame_coordinate==Approx(fps_driven ? 6.0 : 3.3));
        // FPS addition over a caught-up interval can group finite floating
        // operations differently from consuming every source individually.
        CHECK(caught_up.gait_sample->frame_coordinate==Approx(every_source.gait_sample->frame_coordinate));
        CHECK(caught_up.gait_sample->sequence==every_source.gait_sample->sequence);
        CHECK(caught_up.gait_sample->body==every_source.gait_sample->body);
        CHECK(caught_up.gait_sample->skin==every_source.gait_sample->skin);
        CHECK(caught_up.gait_sample->controllers==every_source.gait_sample->controllers);
        CHECK(caught_up.gait_sample->blending==every_source.gait_sample->blending);
        CHECK(sparse.sample(sparse_input).gait_sample==caught_up.gait_sample);
    }
}

TEST_CASE("Skipped remote crouch sources cannot bridge actual continuity boundaries",
    "[e10][game][remote-player][crouch-regression]") {
    const auto model=player_model();
    for(int boundary=0;boundary<7;++boundary) {
        CAPTURE(boundary);
        const auto first=player();
        auto second=first; second.origin.x=12.0;
        auto third=second; third.origin.x=24.0;
        auto fourth=third; fourth.origin.x=36.0;
        hl::HalfLifeRemotePlayerPresentation presentation;
        seed(presentation,model,first);
        const auto moved=presentation.sample(context(model,second,second,&first,2U,10.1,10.1));
        REQUIRE(moved.gait_sample);
        CHECK(moved.gait_sample->frame_coordinate==Approx(1.1));
        auto input=context(model,fourth,fourth,&third,4U,10.3,10.3);
        switch(boundary) {
        case 0:input.current_server_seconds=10.7; input.previous_server_seconds=10.6;
            input.sample_server_seconds=10.7; break; // Cached->previous exceeds the gap bound.
        case 1:third.effects=32U; break; // No-interpolation occurred in the missed source.
        case 2:third.origin.x=300.0; fourth.origin.x=312.0; break;
        case 3:input.model_revision.primary=20U; break;
        case 4:input.entity_lifetime=2U; break;
        case 5:input.network_generation=2U; break;
        case 6:input.discontinuity=true; break;
        }
        const auto reset=presentation.sample(input);
        REQUIRE(reset.status==api::RemotePlayerPresentationStatus::ready);
        REQUIRE(reset.gait_sample);
        CHECK(reset.gait_sample->frame_coordinate==Approx(0.0));
        CHECK_FALSE(reset.previous_sample);
    }
}

TEST_CASE("Remote sequence transition starts once from committed change and expires by time",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    const auto before=player();
    hl::HalfLifeRemotePlayerPresentation presentation;
    seed(presentation,model,before);
    auto after=before; after.sequence=2U; after.animation_time_seconds=10.1;
    const auto first=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
    REQUIRE(first.previous_sample);
    CHECK(first.previous_sample->sequence==0U);
    CHECK(first.sample.sequence==2U);
    CHECK(first.previous_weight==Approx(1.0));
    CHECK(first.transition_identity==2U);
    const auto half=presentation.sample(context(model,after,after,&before,2U,10.1,10.2));
    REQUIRE(half.previous_sample);
    CHECK(half.previous_weight==Approx(0.5));
    CHECK(half.transition_identity==2U);
    CHECK(presentation.sample(context(model,after,after,&before,2U,10.1,10.2)).previous_weight==half.previous_weight);
    CHECK_FALSE(presentation.sample(context(model,after,after,&before,2U,10.1,10.31)).previous_sample);
    auto next=after; next.origin.x=12.0;
    const auto next_output=presentation.sample(context(model,next,next,&after,3U,10.2,10.2));
    CHECK(next_output.transition_identity==2U);
    CHECK(next_output.previous_weight==Approx(0.5));
}

TEST_CASE("Remote identity boundaries exclude local and do not guess missing player fields",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    auto entity=player();
    hl::HalfLifeRemotePlayerPresentation presentation;
    auto input=context(model,entity,entity);
    input.receiving_entity=2U;
    CHECK(presentation.sample(input).status==api::RemotePlayerPresentationStatus::local_excluded);
    input.receiving_entity.reset();
    CHECK(presentation.sample(input).status==api::RemotePlayerPresentationStatus::missing_fields);
    entity.player_movement_schema=false;
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::not_player);
    entity=player(33U);
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::not_player);
    entity=player(); entity.gait_sequence.reset();
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::missing_fields);
    entity=player(); entity.animation_time_seconds.reset();
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::missing_fields);
    entity=player(); entity.animation_time_seconds=-1.0e300;
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::missing_fields);
    entity=player(); entity.controllers[2].reset();
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::missing_fields);
    entity=player(); entity.sequence=99U;
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::invalid_sequence);
    entity=player(); entity.gait_sequence=99U;
    CHECK(presentation.sample(context(model,entity,entity)).status==api::RemotePlayerPresentationStatus::invalid_sequence);
    entity=player(); entity.gait_sequence=0U;
    CHECK_FALSE(presentation.sample(context(model,entity,entity)).gait_sample);
}

TEST_CASE("Remote source ordering uses committed ordinals and treats identities as opaque keys",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    const auto before=player();
    hl::HalfLifeRemotePlayerPresentation presentation;
    auto first=context(model,before,before);
    first.current_record_identity=900U;
    REQUIRE(presentation.sample(first).status==api::RemotePlayerPresentationStatus::ready);
    auto after=before; after.origin.x=12.0;
    auto next=context(model,after,after,&before,2U,10.1,10.1);
    next.current_record_identity=100U; next.previous_record_identity=900U;
    const auto output=presentation.sample(next);
    REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(output.gait_sample);
    CHECK(output.gait_sample->frame_coordinate==Approx(1.1));
    CHECK(presentation.sample(first).status==api::RemotePlayerPresentationStatus::stale_source);
    auto inconsistent=next; inconsistent.current_record_identity=101U;
    CHECK(presentation.sample(inconsistent).status==api::RemotePlayerPresentationStatus::invalid_context);
    auto missing_ordinal=next; missing_ordinal.current_record_ordinal=0U;
    CHECK(presentation.sample(missing_ordinal).status==api::RemotePlayerPresentationStatus::invalid_context);
    CHECK(presentation.sample(next).gait_sample==output.gait_sample);
}

TEST_CASE("Unsupported remote skeletons never use unchecked parent or fixed bone ordinals",
    "[e10][game][remote-player]") {
    const auto entity=player();
    for(int defect=0;defect<7;++defect) {
        CAPTURE(defect);
        auto model=player_model();
        switch(defect) {
        case 0:model.bones[2].name="project_unknown_spine"; break;
        case 1:model.bones[3].name="Bip01 Spine"; break;
        case 2:model.bones[2].parent_index=6; break;
        case 3:model.bones[6].parent_index=99; break;
        case 4:model.bones[1].parent_index=6; break;
        case 5:model.bone_controllers[0].source_type=0x8; break;
        case 6:model.bone_controllers[0].controller_index=1; break;
        }
        hl::HalfLifeRemotePlayerPresentation presentation;
        const auto output=presentation.sample(context(model,entity,entity));
        CHECK(output.status==api::RemotePlayerPresentationStatus::unsupported_skeleton);
        CHECK_FALSE(output.gait_sample);
        CHECK(output.bone_count==0U);
    }
}

TEST_CASE("Half-Life torso inputs validate imported rotational axes and record ordinals",
    "[e10][game][remote-player][pose-handoff]") {
    for(const std::uint32_t axis:std::array{0x8U,0x10U,0x20U}) {
        CAPTURE(axis);
        auto model=player_model();
        const std::size_t channel=axis==0x8U ? 3U : axis==0x10U ? 4U : 5U;
        for(auto& controller:model.bone_controllers) controller.source_type=axis;
        // The record table may start with an unrelated mouth input; torso
        // input0 then addresses record1, not record0 or a fixed bone ordinal.
        model.bone_controllers.insert(model.bone_controllers.begin(),
            assets::ModelBoneController{1,0x20U,0.0F,60.0F,0,4,false});
        model.bones[1].controller_indices[5]=0;
        for(std::size_t i=2U;i<6U;++i) {
            model.bones[i].controller_indices[5]=-1;
            model.bones[i].controller_indices[channel]=static_cast<std::int32_t>(i-1U);
        }
        const auto before=player(); auto after=before; after.origin.y=12.0;
        api::GameClientHost host{hl::make_half_life_client_module()}; host.reset({1U,1U,1U});
        REQUIRE(host.remote_player(context(model,before,before)).status==api::RemotePlayerPresentationStatus::ready);
        const auto intent=host.remote_player(context(model,after,after,&before,2U,10.1,10.1));
        REQUIRE(intent.status==api::RemotePlayerPresentationStatus::ready);
        REQUIRE(intent.gait_sample);
        constexpr std::array<std::uint8_t,4U> quarter_yaw{31U,31U,31U,31U};
        CHECK(intent.sample.controllers==quarter_yaw);
        const studio::StudioPoseModelIdentity identity{"models/project_owned_axis.mdl",{19U,31U}};
        const auto pose=studio::StudioPoseEvaluator{}.compose(identity,model,composition(intent));
        INFO((pose.error ? pose.error->context : "imported torso axis composed"));
        REQUIRE(pose); REQUIRE(pose.pose);
        const auto controller_radians=(31.0*60.0/255.0-30.0)*std::acos(-1.0)/180.0;
        const auto expected_component=std::sin(controller_radians/2.0);
        const auto& rotation=pose.pose->local_bones()[2].rotation;
        CHECK(rotation.x==Approx(channel==3U ? expected_component : 0.0).margin(1e-5));
        CHECK(rotation.y==Approx(channel==4U ? expected_component : 0.0).margin(1e-5));
        CHECK(rotation.z==Approx(channel==5U ? expected_component : 0.0).margin(1e-5));
        CHECK(rotation.w==Approx(std::cos(controller_radians/2.0)).margin(1e-5));
        host.teardown();
        model.bone_controllers[1].source_type=0x28U; // Ambiguous XR|ZR must not select either.
        hl::HalfLifeRemotePlayerPresentation invalid;
        CHECK(invalid.sample(context(model,before,before)).status==
            api::RemotePlayerPresentationStatus::unsupported_skeleton);
    }
}

TEST_CASE("Remote lifetime model discontinuity gap and generation reset do not inherit gait",
    "[e10][game][remote-player][crouch-regression]") {
    const auto model=player_model();
    const auto before=player();
    auto after=before; after.origin.x=12.0;
    for(int boundary=0;boundary<7;++boundary) {
        hl::HalfLifeRemotePlayerPresentation presentation;
        seed(presentation,model,before);
        const auto moved=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
        REQUIRE(moved.gait_sample); CHECK(moved.gait_sample->frame_coordinate==Approx(1.1));
        // A coherent zero-displacement pair would retain the previous phase if
        // the explicit identity/reset boundary were accidentally ignored.
        auto current=after;
        if(boundary==6) current.effects=32U;
        auto input=context(model,current,current,&after,3U,10.2,10.2);
        switch(boundary) {
        case 0:input.entity_lifetime=2U; break;
        case 1:input.model_revision.primary=20U; break;
        case 2:input.discontinuity=true; break;
        case 3:input.network_generation=2U; break;
        case 4:input.map_generation=2U; break;
        case 5:presentation.reset(); break;
        case 6:break;
        }
        const auto reset=presentation.sample(input);
        REQUIRE(reset.gait_sample);
        CHECK(reset.gait_sample->frame_coordinate==Approx(0.0));
        CHECK_FALSE(reset.previous_sample);
    }
    hl::HalfLifeRemotePlayerPresentation presentation;
    seed(presentation,model,before);
    auto teleported=before; teleported.origin.x=200.0;
    const auto teleport=presentation.sample(context(model,teleported,teleported,&before,2U,10.1,10.1));
    REQUIRE(teleport.gait_sample); CHECK(teleport.gait_sample->frame_coordinate==Approx(0.0));
    auto gap=context(model,after,after,&before,3U,11.0,11.0); gap.previous_server_seconds=10.0;
    const auto gap_output=presentation.sample(gap);
    REQUIRE(gap_output.gait_sample); CHECK(gap_output.gait_sample->frame_coordinate==Approx(0.0));
    CHECK(presentation.sample(context(model,before,before)).status==api::RemotePlayerPresentationStatus::stale_source);

    auto replacement=after; replacement.origin.x=24.0;
    auto replacement_input=context(model,replacement,replacement,&after,4U,11.1,11.1);
    replacement_input.entity_lifetime=2U;
    replacement_input.previous_server_seconds=11.0;
    const auto replaced=presentation.sample(replacement_input);
    REQUIRE(replaced.gait_sample);
    // Even if the caller accidentally supplies an old occupant's coherent
    // pair, replacing a used lifetime must not integrate its last trajectory.
    CHECK(replaced.gait_sample->frame_coordinate==Approx(0.0));
}

TEST_CASE("Four eight and sixteen remote players keep independent bounded gait state",
    "[e10][game][remote-player]") {
    const auto model=player_model();
    for(const std::uint32_t count:std::array{4U,8U,16U}) {
        hl::HalfLifeRemotePlayerPresentation presentation;
        for(std::uint32_t i=0;i<count;++i) {
            auto before=player(i+2U);
            seed(presentation,model,before);
            auto after=before; after.origin.x=static_cast<double>(i+1U);
            const auto output=presentation.sample(context(model,after,after,&before,2U,10.1,10.1));
            REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
            REQUIRE(output.gait_sample);
            CHECK(output.gait_sample->frame_coordinate==Approx(static_cast<double>(i+1U)*11.0/120.0));
        }
    }
}

TEST_CASE("Opt-in exact-root approved player model satisfies production Half-Life remote policy",
    "[e10][game][remote-player][installed-player]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr; std::size_t count=0U;
    if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
    std::free(value);
#else
    if(const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT")) root=value;
#endif
    if(root.empty()) SKIP("No opt-in read-only local game root");
    const std::filesystem::path allowed_root{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"};
    if(root.lexically_normal()!=allowed_root.lexically_normal())
        SKIP("Optional player control authorizes only the exact prepared research root");
    namespace local=hlclient::local_resources;
    namespace goldsrc=hlclient::goldsrc;
    namespace readiness=hlclient::tests::readiness_fixture;
    auto roots=local::LocalResourceSearchRoots::create(root,"valve");
    if(!roots || !roots.roots) SKIP("Prepared local player root is absent or not authorized");
    auto created=local::LocalResourceEnvironment::create(std::move(*roots.roots));
    if(!created || !created.environment) SKIP("Prepared resource environment is unavailable");
    std::shared_ptr<const local::LocalResourceEnvironment> environment{std::move(created.environment)};
    // Test-owned manifest slots, not evidence about a server's precache table.
    // The world entry is correlated metadata only; this test opens one model.
    const auto resources=readiness::parse_resource_list({
        {2U,"maps/crossfire.bsp",9U,0U,0U}, {2U,"models/player.mdl",7U,0U,0U}});
    const auto inventory=goldsrc::LocalResourceInventoryBuilder{}.build(resources,
        goldsrc::GoldSrcResourceNameMapper{},environment->resolver());
    if(!inventory || !inventory.state) SKIP("Approved local player inventory is unavailable");
    const auto manifest=readiness::build_manifest(resources,*inventory.state,
        readiness::parse_server_info("maps/crossfire.bsp"),*environment);
    REQUIRE(manifest); REQUIRE(manifest.state);
    const auto* entry=manifest.state->find(goldsrc::ResourceType::model,7U);
    REQUIRE(entry);
    if(!entry->locator()) SKIP("models/player.mdl is absent or rejected by approved inventory");
    const auto plan=goldsrc::AssetDispatchPlanBuilder{}.build(*manifest.state,*entry);
    REQUIRE(plan); REQUIRE(plan.plan);
    goldsrc::ApprovedAssetSourceOpener opener;
    auto begun=opener.begin(*plan.plan,environment);
    if(!begun || !begun.operation) SKIP("Approved local player open was rejected");
    auto& operation=*begun.operation;
    for(std::size_t i=0;i<1024U;++i) {
        if(operation.state()==goldsrc::ApprovedAssetSourceOpenState::source_ready ||
            operation.state()==goldsrc::ApprovedAssetSourceOpenState::failed ||
            operation.state()==goldsrc::ApprovedAssetSourceOpenState::timed_out) break;
        operation.update(goldsrc::ApprovedAssetSourceOpenTimePoint{}+
            std::chrono::milliseconds{static_cast<std::int64_t>(i)});
    }
    if(operation.state()!=goldsrc::ApprovedAssetSourceOpenState::source_ready)
        SKIP("Approved local player read did not reach source_ready");
    auto source=operation.take_result(); REQUIRE(source);
    assets::AssetImporterRegistries registries;
    REQUIRE(goldsrc::register_builtin_asset_importers(registries));
    const auto imported=goldsrc::ApprovedAssetImporterDispatcher{registries}.dispatch(*source,*plan.plan);
    if(imported.error && imported.error->code==assets::AssetErrorCode::ExternalDependencyRequired)
        SKIP("Optional player importer requires a separately approved companion bundle");
    INFO((imported.error ? imported.error->context : "approved player imported"));
    REQUIRE(imported.imported()); REQUIRE(imported.asset);
    const auto* asset=std::get_if<assets::ModelAsset>(&*imported.asset);
    REQUIRE(asset); REQUIRE(asset->skeletal_data);
    const auto& model=*asset->skeletal_data;
    REQUIRE(model.sequences.size()>1U); REQUIRE_FALSE(model.skin_families.empty());
    auto entity=player();
    api::GameClientHost host{hl::make_half_life_client_module()}; host.reset({1U,1U,1U});
    auto input=context(model,entity,entity);
    input.model_revision=goldsrc::visual_assets::goldsrc_studio_source_fingerprint(source->source().bytes());
    const auto output=host.remote_player(input);
    REQUIRE(output.status==api::RemotePlayerPresentationStatus::ready);
    REQUIRE(output.gait_sample);
    CHECK(output.bone_count==model.bones.size());
    CHECK(std::isfinite(output.sample.frame_coordinate));
    CHECK(std::isfinite(output.gait_sample->frame_coordinate));
    const studio::StudioPoseModelIdentity pose_identity{"models/player.mdl",input.model_revision};
    studio::StudioPoseEvaluator evaluator;
    const auto posed=evaluator.compose(pose_identity,model,composition(output));
    INFO((posed.error ? posed.error->context : "approved actual-player pose composed"));
    REQUIRE(posed); REQUIRE(posed.pose);
    CHECK(posed.pose->world_bones().size()==model.bones.size());
    for(const auto& bone:posed.pose->world_bones())
        for(const auto component:bone.transform.values) CHECK(std::isfinite(component));
    std::size_t selected_vertices=0U;
    for(const auto& part:posed.pose->body_selection().bodyparts)
        selected_vertices+=model.submodels[part.submodel_index].vertices.size();
    if(selected_vertices) {
        const auto bounds=evaluator.posed_bounds(pose_identity,model,*posed.pose);
        INFO((bounds.error ? bounds.error->context : "approved actual-player posed bounds"));
        REQUIRE(bounds); REQUIRE(bounds.bounds);
        for(const auto component:std::array{bounds.bounds->minimum.x,bounds.bounds->minimum.y,
            bounds.bounds->minimum.z,bounds.bounds->maximum.x,bounds.bounds->maximum.y,
            bounds.bounds->maximum.z}) CHECK(std::isfinite(component));
        CHECK(bounds.bounds->minimum.x<=bounds.bounds->maximum.x);
        CHECK(bounds.bounds->minimum.y<=bounds.bounds->maximum.y);
        CHECK(bounds.bounds->minimum.z<=bounds.bounds->maximum.z);
    } else INFO("Approved player pose has no selected vertices; bounds control unavailable");
    std::cout << "offline_player_control virtual_resource=models/player.mdl approved=true bones="
        << model.bones.size() << " controllers=" << model.bone_controllers.size()
        << " sequences=" << model.sequences.size() << " policy=ready pose_valid=true bounds_vertices="
        << selected_vertices << " live_state=false\n";
    host.teardown();
}
