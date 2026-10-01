#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/remote_player.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_source_bundle.hpp>

#include "local_resource_readiness_test_fixture.hpp"

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
