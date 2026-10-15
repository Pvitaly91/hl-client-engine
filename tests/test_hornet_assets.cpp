#include "crossbow_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace hornet_assets {
namespace api=hlclient::game_api;
namespace g=hlclient::goldsrc;
namespace c=hlclient::client;
namespace t=hlclient::tests;
namespace f=t::crossbow_fixture;
void stamp(c::RuntimeClientObservationState& s,std::size_t record,double seconds) {
    s.publication_revision=record;s.server_time_seconds=seconds;
    s.entity_metadata=s.server_time_metadata=s.client_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{record,record,0,0,8}};
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
}
api::LocalSoundReference sound(std::string_view name) {
    api::LocalSoundReference result;
    result.source=api::SoundReferenceSource::module_profile_dependency;
    std::copy_n(name.begin(),std::min(name.size(),result.sample.size()),result.sample.begin());
    return result;
}
c::RuntimeClientObservationState state() {
    auto s=f::state(-1,1,8);s.weapon_hud.active_weapon_id=11;s.weapon_hud.clips[11]=-1;
    auto& type=s.weapon_hud.catalogue.front();type.id=11;type.command_name="weapon_hornetgun";
    type.slot=3;type.position=3;s.receiving_client->owned_weapon_bits=1U<<11;
    s.weapon_slots.front().weapon_id=11;
    s.entity_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{1,1,0,0,8}};
    s.server_time_metadata=s.client_metadata=s.entity_metadata;s.server_time_seconds=0;
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);return s;
}
template<class Request> g::SoundAssetResult ready(Request request) {
    auto result=request();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{2};
    while(result.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});result=request();
    }
    return result;
}
}

TEST_CASE("E23 module sound dependency syntax stays bounded and separate from server or model strings",
          "[e23][no-stock][audio][game-api]") {
    using namespace hornet_assets;
    for(const auto token:{"agrunt/ag_fire1.wav","agrunt/ag_fire2.wav","agrunt/ag_fire3.wav"}) {
        auto reference=sound(token);CHECK(g::valid_local_sound_reference(reference));
        reference.source=api::SoundReferenceSource::validated_local_model_event;
        CHECK_FALSE(g::valid_local_sound_reference(reference));
        reference.source=api::SoundReferenceSource::server_precache_reference;
        CHECK_FALSE(g::valid_local_sound_reference(reference));
    }
    for(const auto token:{"../outside.wav","/absolute.wav","C:/test.wav","sound/agrunt/ag_fire1.wav",
            "sound/ag_fire1.wav","agrunt/../x.wav","agrunt//x.wav","agrunt/x.WAV",
            "agrunt/x:stream.wav","agrunt/x/y.wav","agrunt/.wav","agrunt/x.wav/"})
        CHECK_FALSE(g::valid_local_sound_reference(sound(token)));
    api::LocalSoundReference unterminated;unterminated.source=api::SoundReferenceSource::module_profile_dependency;
    unterminated.sample.fill('x');CHECK_FALSE(g::valid_local_sound_reference(unterminated));
}

TEST_CASE("E23 approved read-only Hivehand assets use normal import viewmodel and audio paths",
          "[e23][optional-assets][hornet-assets]") {
    using namespace hornet_assets;
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count=0;
    if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;
    std::free(value);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("Exact approved read-only root opt-in absent");
    auto resources=t::readiness_fixture::parse_resource_list({
        {2,"maps/crossfire.bsp",9,0,0},{2,"models/v_hgun.mdl",73,0,0},
        {2,"models/p_hgun.mdl",71,0,0},{2,"models/w_hgun.mdl",74,0,0},
        {2,"models/player.mdl",7,0,0},
        {2,"models/hornet.mdl",82,0,0},{2,"sprites/laserbeam.spr",86,0,0},
        {0,"hornet/ag_buzz1.wav",101,0,0},{0,"hornet/ag_buzz2.wav",107,0,0},
        {0,"hornet/ag_buzz3.wav",115,0,0},{0,"hornet/ag_hornethit1.wav",120,0,0},
        {0,"hornet/ag_hornethit2.wav",127,0,0},{0,"hornet/ag_hornethit3.wav",132,0,0}});
    auto made=hlclient::app::RuntimeReplayLocalAssets::create(resources,
        t::readiness_fixture::parse_server_info("maps/crossfire.bsp"),root,"valve",
        hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(made.projection);c::ClientWorldState world;auto s=state();
    REQUIRE(made.projection->reset_generation(s,world));
    const auto metadata=made.projection->presentation_model(1,73);REQUIRE(metadata);
    REQUIRE(hlclient::games::halflife::hornet_sequences(*metadata));
    for(const unsigned slot:{73U,71U,74U,82U}) {
        const auto model=made.projection->studio_model_data(slot);REQUIRE(model);
        std::cout<<"hornet-model slot="<<slot<<" bones="<<model->bones.size()
            <<" attachments="<<model->attachments.size()<<" bodyparts="<<model->bodyparts.size()
            <<" skins="<<model->skin_families.size()<<" sequences="<<model->sequences.size()<<'\n';
        for(std::size_t i=0;i<model->sequences.size();++i) {
            const auto& sequence=model->sequences[i];
            std::cout<<"hornet-sequence "<<slot<<" "<<i<<" "<<sequence.label
                <<" frames="<<sequence.frame_count<<" fps="<<sequence.frames_per_second<<'\n';
        }
        for(const auto& attachment:model->attachments) {
            CHECK(attachment.bone_index<model->bones.size());
            CHECK(std::isfinite(attachment.local_origin.x));CHECK(std::isfinite(attachment.local_origin.y));
            CHECK(std::isfinite(attachment.local_origin.z));
            std::cout<<"hornet-attachment "<<slot<<" bone="<<attachment.bone_index<<" xyz="
                <<attachment.local_origin.x<<","<<attachment.local_origin.y<<","<<attachment.local_origin.z<<'\n';
        }
        hlclient::assets::AssetVector3 lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};
        for(const auto& sub:model->submodels)for(const auto& vertex:sub.vertices) {
            const auto p=vertex.source_position;
            lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
            hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
        }
        std::cout<<"hornet-bounds "<<slot<<" "<<lo.x<<","<<lo.y<<","<<lo.z<<" "
            <<hi.x<<","<<hi.y<<","<<hi.z<<'\n';
    }
    g::ApprovedSoundAssets loader(made.projection->sound_resources());
    const auto prove_pcm=[](const g::SoundAssetResult& loaded) {
        REQUIRE(loaded.status==g::SoundAssetStatus::ready);REQUIRE(loaded.asset);
        REQUIRE(std::ranges::any_of(loaded.asset->interleaved_samples,[](float v){return std::isfinite(v)&&v!=0;}));
        hlclient::audio::OfflineOutput output;hlclient::audio::Command command;
        command.voice=1;command.asset=loaded.asset;command.local=true;REQUIRE(output.submit(command));
        std::vector<float> pcm(loaded.asset->interleaved_samples.size()*4+4096);output.mixer.render(pcm);
        CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
        CHECK(std::ranges::any_of(pcm,[](float v){return v!=0;}));
        std::cout<<"hornet-pcm rate="<<loaded.asset->sample_rate<<" channels="
            <<loaded.asset->channel_count<<" samples="<<loaded.asset->interleaved_samples.size()<<'\n';
    };
    for(const auto token:{"agrunt/ag_fire1.wav","agrunt/ag_fire2.wav","agrunt/ag_fire3.wav"}) {
        std::cout<<"hornet-launch "<<token<<'\n';prove_pcm(ready([&]{return loader.request_local(sound(token));}));
    }
    for(const unsigned slot:{101U,107U,115U,120U,127U,132U}) {
        std::cout<<"hornet-server-sound slot="<<slot<<'\n';
        prove_pcm(ready([&]{return loader.request(static_cast<std::uint16_t>(slot));}));
    }
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,1});host.bind_model(metadata);host.observe(s,0);
    auto gl=t::entity_opengl_fixture::try_context(320,240);
    if(!gl || !t::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable for approved Hivehand control");
    gl->initialize_renderer();hlclient::renderer::RenderScene scene;
    scene.camera={{100,80,80},{0,80,80},{0,0,1},1.04719755F,.1F,4096};
    gl->renderer().render(scene,{320,240});const auto empty=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    for(const double age:{.01,.08,.16}) {
        api::LocalWeaponVisual visual{5,1,1,.02,api::LocalWeaponAnimationSource::provisional_command,api::LocalWeaponActionStatus::predicted_pending};
        const auto intent=host.viewmodel(s,metadata,.02+age,visual);REQUIRE(intent.status==api::ViewmodelStatus::ready);
        scene.first_person_entities=made.projection->materialize_viewmodel(intent);REQUIRE(scene.first_person_entities);
        gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        const auto pixels=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
        CHECK(pixels.rgba8!=empty.rgba8);
    }
    // The ordinary E11 application composition must merge this actual child,
    // not merely return its numeric weapon-model slot. Isolate the controlled
    // test scene outside BSP PVS and omit the map from GPU A/B draws.
    const auto player=made.projection->studio_model_data(7),weapon=made.projection->studio_model_data(71);
    REQUIRE(player);REQUIRE(weapon);REQUIRE_FALSE(player->sequences.empty());
    c::RuntimePacketEntityObservation remote;remote.entity_number=2;
    remote.ordinary_visual_schema=remote.player_movement_schema=true;
    remote.model_index=7;remote.weapon_model_index=71;remote.origin={8192,8192,8192};
    remote.angles={0,0,0};remote.sequence=0;remote.frame=0;remote.animation_time_seconds=1;
    remote.frame_rate=1;remote.body=remote.skin=remote.gait_sequence=0;
    remote.controllers.fill(127);remote.blending.fill(127);
    s.packet_entities={remote};stamp(s,10,1);REQUIRE(c::valid_runtime_observation(s));
    REQUIRE(made.projection->project_entities(s,world));
    world.set_camera({{8288,8128,8228},{8192,8192,8196},{0,0,1}});
    REQUIRE(made.projection->present_entities(host,10,1,world,{320,240}));
    REQUIRE(made.projection->present_entities(host,10.21,1,world,{320,240}));
    const auto frame=world.entity_frame();REQUIRE(frame);REQUIRE(frame->studio_instances().size()==2);
    const auto& parent=frame->studio_instances()[0];const auto& child=frame->studio_instances()[1];
    CHECK(parent.entity_number==2);CHECK(child.entity_number==2);CHECK(child.subinstance_ordinal==1);
    const auto& parent_pose=frame->studio_poses()[parent.pose_index].bone_matrices;
    const auto& child_pose=frame->studio_poses()[child.pose_index].bone_matrices;
    REQUIRE(parent_pose.size()==player->bones.size());REQUIRE(child_pose.size()==weapon->bones.size());
    std::size_t matched{};
    for(std::size_t i=0;i<weapon->bones.size();++i) {
        CHECK(std::ranges::all_of(child_pose[i],[](float value){return std::isfinite(value);}));
        const auto bone=std::ranges::find(player->bones,weapon->bones[i].name,&hlclient::assets::ModelBone::name);
        if(bone!=player->bones.end()) {++matched;CHECK(child_pose[i]==parent_pose[static_cast<std::size_t>(bone-player->bones.begin())]);}
    }
    REQUIRE(matched>0);std::cout<<"hornet-pmodel merged-bones="<<matched<<" child-bones="<<child_pose.size()<<'\n';
    scene=c::build_render_scene(world);scene.static_world.reset();scene.clear_color={0,0,0,1};
    const auto draw=[&](const hlclient::renderer::RenderScene& image) {
        gl->renderer().render(image,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        return gl->renderer().observe_framebuffer({320,240},image.clear_color,true);
    };
    const auto full=draw(scene);REQUIRE(full.non_clear_pixel_count>0);
    hlclient::entity_render::EntityRenderFrameBuildInput body;body.resource_id=1;body.resource_revision=1;
    body.interpolation=frame->interpolation();body.studio_instances.push_back(parent);
    body.studio_poses.assign(frame->studio_poses().begin(),frame->studio_poses().end());
    auto built=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*world.entity_scene(),std::move(body));REQUIRE(built);
    auto body_only=scene;body_only.dynamic_entities->frame=
        std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*built.frame));
    CHECK(full.rgba8!=draw(body_only).rgba8); // Actual p_hgun contributes GPU pixels.
    const auto merged_uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
    CHECK(full.rgba8==draw(scene).rgba8);CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==merged_uploads);

    c::RuntimePacketEntityObservation hornet;hornet.entity_number=40;hornet.model_index=82;
    hornet.ordinary_visual_schema=true;hornet.brush_move_type=5;hornet.sequence=0;hornet.frame=0;
    hornet.origin={8188,8189,8192};hornet.angles={0,0,0};hornet.body=hornet.skin=0;
    s.packet_entities={hornet};stamp(s,11,1.2);REQUIRE(made.projection->project_entities(s,world));
    REQUIRE(world.entity_frame()->studio_instances().size()==1);
    scene={};scene.clear_color={0,0,0,1};scene.camera={{8192,8192,8212},{8192,8192,8192},{0,1,0},1.04719755F,.1F,128};
    const auto projectile_baseline=draw(scene);
    scene.dynamic_entities=hlclient::renderer::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    const auto projectile=draw(scene);CHECK(projectile.rgba8!=projectile_baseline.rgba8);CHECK(projectile.non_clear_pixel_count>0);
    const auto model_uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
    CHECK(projectile.rgba8==draw(scene).rgba8);CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==model_uploads);
    // The actual approved laserbeam sprite must decode/materialize/upload;
    // listing its manifest slot alone is not trail-resource evidence.
    c::CommittedTemporaryEffect follow;follow.generation=1;follow.record=follow.record_ordinal=11;
    follow.kind=c::TemporaryEffectKind::beam_follow;follow.entity_attachment=40;follow.sprite_index=86;
    follow.received_at_seconds=.3;follow.life_tenths=10;follow.width_tenths=2;
    follow.color={179,39,14};follow.brightness=128;
    made.projection->consume_follow(follow,.3,&host);
    scene.dynamic_entities.reset();made.projection->present_entity_effects(scene,.3);
    s.packet_entities[0].origin.x=8196;stamp(s,12,1.3);REQUIRE(made.projection->project_entities(s,world));
    made.projection->present_entity_effects(scene,.4);REQUIRE(scene.follow_trail_entities);
    REQUIRE(scene.follow_trail_entities->frame->sprite_instances().size()==1);
    const auto& ribbon=scene.follow_trail_entities->frame->sprite_instances()[0];
    REQUIRE(ribbon.ribbon_end);CHECK(ribbon.ribbon_width==.2F);
    const auto trail=draw(scene);CHECK(trail.rgba8!=projectile_baseline.rgba8);
    CHECK(trail.non_clear_pixel_count>0);CHECK(trail.non_clear_pixel_count<320U*240U/4U);
    const auto sprite_uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;
    CHECK(trail.rgba8==draw(scene).rgba8);CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==sprite_uploads);
    s.packet_entities.clear();stamp(s,13,1.4);REQUIRE(made.projection->project_entities(s,world));
    made.projection->present_entity_effects(scene,1.5);CHECK_FALSE(scene.follow_trail_entities);
    CHECK(draw(scene).rgba8==projectile_baseline.rgba8);
    gl->release_renderer();
}
