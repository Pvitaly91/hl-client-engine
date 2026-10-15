#include "hand_grenade_world_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/games/halflife/hand_grenade_profile.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
namespace t=hlclient::tests;namespace f=t::hand_grenade_world_fixture;namespace api=hlclient::game_api;
namespace c=hlclient::client;namespace g=hlclient::goldsrc;namespace r=hlclient::renderer;
template<class Request> g::SoundAssetResult ready(Request request) {
    auto result=request();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{2};
    while(result.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});result=request();
    }
    return result;
}
double ac_rms(std::span<const float> samples,std::size_t stride=1,std::size_t channel=0) {
    double mean{};std::size_t count{};
    for(std::size_t i=channel;i<samples.size();i+=stride){mean+=samples[i];++count;}
    REQUIRE(count>0);mean/=static_cast<double>(count);double variance{};
    for(std::size_t i=channel;i<samples.size();i+=stride){const auto delta=samples[i]-mean;variance+=delta*delta;}
    return std::sqrt(variance/static_cast<double>(count));
}
bool different_pixels(const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b) { return a!=b; }
}

TEST_CASE("E24 approved read-only grenade models bounce debris and null use normal import merge GL and mixer paths",
          "[e24][optional-assets][hand-grenade-assets]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count=0;
    if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;
    std::free(value);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("Exact approved read-only research root opt-in absent");
    auto resources=t::readiness_fixture::parse_resource_list({
        {2,"maps/crossfire.bsp",9,0,0},{2,"models/v_grenade.mdl",73,0,0},
        {2,"models/p_grenade.mdl",71,0,0},{2,"models/w_grenade.mdl",237,0,0},{2,"models/grenade.mdl",312,0,0},
        {2,"models/player.mdl",7,0,0},{2,"sprites/zerogxplode.spr",319,0,0},{2,"sprites/steam1.spr",320,0,0},
        {0,"weapons/grenade_hit1.wav",101,0,0},{0,"weapons/grenade_hit2.wav",107,0,0},
        {0,"weapons/grenade_hit3.wav",115,0,0},{0,"weapons/debris1.wav",120,0,0},
        {0,"weapons/debris2.wav",127,0,0},{0,"weapons/debris3.wav",132,0,0},{0,"common/null.wav",140,0,0}});
    auto made=hlclient::app::RuntimeReplayLocalAssets::create(resources,
        t::readiness_fixture::parse_server_info("maps/crossfire.bsp"),root,"valve",
        hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(made.projection);c::ClientWorldState world;auto s=f::state();s.packet_entities.clear();f::stamp(s,1,1);
    REQUIRE(made.projection->reset_generation(s,world));
    const auto metadata=made.projection->presentation_model(1,73);REQUIRE(metadata);
    REQUIRE(hlclient::games::halflife::hand_grenade_sequences(*metadata));
    for(const unsigned slot:{73U,71U,237U}) {
        const auto model=made.projection->studio_model_data(slot);REQUIRE(model);
        std::cout<<"hand-grenade-model slot="<<slot<<" bones="<<model->bones.size()
            <<" controllers="<<model->bone_controllers.size()<<" attachments="<<model->attachments.size()
            <<" bodyparts="<<model->bodyparts.size()<<" skins="<<model->skin_families.size()<<" sequences="<<model->sequences.size()<<'\n';
        for(std::size_t i=0;i<model->sequences.size();++i) {
            const auto& sequence=model->sequences[i];
            std::cout<<"hand-grenade-sequence "<<slot<<" "<<i<<" "<<sequence.label
                <<" frames="<<sequence.frame_count<<" fps="<<sequence.frames_per_second<<'\n';
        }
        for(const auto& attachment:model->attachments) {
            CHECK(attachment.bone_index<model->bones.size());
            std::cout<<"hand-grenade-attachment "<<slot<<" bone="<<attachment.bone_index<<" xyz="
                <<attachment.local_origin.x<<","<<attachment.local_origin.y<<","<<attachment.local_origin.z<<'\n';
        }
        hlclient::assets::AssetVector3 lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};
        for(const auto& sub:model->submodels)for(const auto& vertex:sub.vertices) {
            const auto p=vertex.source_position;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
            hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
        }
        std::cout<<"hand-grenade-bounds "<<slot<<" "<<lo.x<<","<<lo.y<<","<<lo.z<<" "
            <<hi.x<<","<<hi.y<<","<<hi.z<<'\n';
    }
    const auto timed=made.projection->studio_model_data(237),contact=made.projection->studio_model_data(312);
    REQUIRE(timed);REQUIRE(contact);CHECK(timed!=contact);REQUIRE(timed->sequences.size()>6);
    g::ApprovedSoundAssets loader(made.projection->sound_resources());
    unsigned ordinal{};
    for(const unsigned slot:{101U,107U,115U,120U,127U,132U,140U}) {
        const auto loaded=ready([&]{return loader.request(static_cast<std::uint16_t>(slot));});
        REQUIRE(loaded.status==g::SoundAssetStatus::ready);REQUIRE(loaded.asset);REQUIRE_FALSE(loaded.asset->interleaved_samples.empty());
        REQUIRE(std::ranges::all_of(loaded.asset->interleaved_samples,[](float v){return std::isfinite(v);}));
        double peak{};for(const auto sample:loaded.asset->interleaved_samples)peak=std::max(peak,double(std::abs(sample)));
        std::cout<<"hand-grenade-server-pcm slot="<<slot<<" rate="<<loaded.asset->sample_rate
            <<" channels="<<loaded.asset->channel_count<<" samples="<<loaded.asset->interleaved_samples.size()<<" peak="<<peak<<'\n';
        if(slot==140) {
            // The approved 8-bit null WAV contains adjacent127/128 levels,
            // not mathematical silence. Preserve the original one-LSB range;
            // do not add a filename-specific importer or engine mute.
            const auto [lo,hi]=std::minmax_element(loaded.asset->interleaved_samples.begin(),loaded.asset->interleaved_samples.end());
            const auto low_count=std::ranges::count(loaded.asset->interleaved_samples,-1.0F/128);
            const auto zero_count=std::ranges::count(loaded.asset->interleaved_samples,0.0F);
            std::cout<<"hand-grenade-null min="<<*lo<<" max="<<*hi<<" ac-rms="<<ac_rms(loaded.asset->interleaved_samples)
                <<" minus-one-lsb-count="<<low_count<<" zero-count="<<zero_count<<'\n';
            CHECK(low_count>0);CHECK(zero_count>0);
            CHECK(static_cast<std::size_t>(low_count+zero_count)==loaded.asset->interleaved_samples.size());
            CHECK(*lo==-1.0F/128);CHECK(*hi==0);CHECK(peak==Catch::Approx(1.0/128));
            // Any distribution within this one-LSB interval has sigma <= range/2.
            CHECK(ac_rms(loaded.asset->interleaved_samples)<=1.0/256);
        } else CHECK(peak>.001);
        f::Sink sink;g::ServerAudio playback(sink);playback.present({},{{},{0,-1,0},1,false},{},1);
        auto event=f::sound(++ordinal,40,slot,-50,slot==140 ? 1 : 2);event.sound.volume=slot<120 ? 64 : 140;
        playback.consume(event,{});playback.update(&loader,{});REQUIRE(sink.starts.size()==1);
        CHECK(sink.starts.front().asset==loaded.asset);CHECK(sink.starts.front().origin.y==-50);
        std::vector<float> pcm(loaded.asset->interleaved_samples.size()*5+4096);sink.output.mixer.render(pcm);
        CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
        if(slot==140) {
            const auto& voice=sink.starts.front();CHECK_FALSE(voice.local);
            CHECK(voice.volume==Catch::Approx(140.0/255));CHECK(voice.attenuation==Catch::Approx(51.0/64));
            const double bound=peak*2*voice.volume*std::max(0.0,1.0-50.0*voice.attenuation/1000.0);
            REQUIRE(bound>0);
            CHECK(std::ranges::all_of(pcm,[&](float v){return v>=-bound-1e-8 && v<=1e-8;}));
            const auto steady=std::span<const float>{pcm}.first(1024);
            CHECK(ac_rms(steady,2,0)==0);CHECK(ac_rms(steady,2,1)<=bound/2+1e-8);
        } else CHECK(std::ranges::any_of(pcm,[](float v){return std::abs(v)>.00001F;}));
    }
    // Named-channel replacement stops the preceding audible grenade waveform;
    // only the original null asset's bounded one-LSB quantization remains.
    {
        f::Sink sink;g::ServerAudio playback(sink);playback.present({},{{},{0,-1,0},1,false},{},1);
        auto audible=f::sound(1,40,101,-50,1);playback.consume(audible,{});playback.update(&loader,{});
        auto null=f::sound(2,40,140,-50,1);playback.consume(null,{});playback.update(&loader,{});
        std::array<float,1024> pcm{};sink.output.mixer.render(pcm);CHECK(playback.statistics().stopped==1);
        REQUIRE(sink.starts.size()==2);const auto& voice=sink.starts.back();CHECK_FALSE(voice.local);
        CHECK(voice.volume==Catch::Approx(140.0/255));CHECK(voice.attenuation==Catch::Approx(51.0/64));
        const double bound=(1.0/128)*2*voice.volume*std::max(0.0,1.0-50.0*voice.attenuation/1000.0);
        REQUIRE(bound>0);
        CHECK(std::ranges::all_of(pcm,[&](float v){return v>=-bound-1e-8 && v<=1e-8;}));
        CHECK(std::ranges::any_of(pcm,[&](float v){return v<-bound*.5;}));
        CHECK(ac_rms(pcm,2,0)==0);CHECK(ac_rms(pcm,2,1)<=bound/2+1e-8);
    }
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,1});host.bind_model(metadata);host.observe(s,0);host.set_movement_audio_time_origin(0);
    auto gl=t::entity_opengl_fixture::try_context(320,240);
    if(!gl || !t::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable for approved hand grenade control");
    gl->initialize_renderer();r::RenderScene scene;scene.camera={{100,80,80},{0,80,80},{0,0,1},1.04719755F,.1F,4096};
    const auto draw=[&](const r::RenderScene& image) {gl->renderer().render(image,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        return gl->renderer().observe_framebuffer({320,240},image.clear_color,true);};
    const auto empty=draw(scene);
    for(const unsigned sequence:{2U,3U,4U,5U}) {
        CAPTURE(sequence);const double start=sequence*1.5;
        api::LocalWeaponVisual visual{sequence,0,sequence+1,start,api::LocalWeaponAnimationSource::provisional_command,api::LocalWeaponActionStatus::predicted_pending};
        std::size_t visible_frames{};
        // Throw sequences start with hands outside the viewport in this real
        // model. Sample deterministic frame coordinates across each complete
        // sequence instead of requiring every early frame to be visible.
        for(std::uint32_t frame=0;frame<metadata->sequences[sequence].frame_count;frame+=3) {
            const auto now=start+frame/metadata->sequences[sequence].fps;
            const auto intent=host.viewmodel(s,metadata,now,visual);REQUIRE(intent.status==api::ViewmodelStatus::ready);
            CHECK(intent.sequence==sequence);CHECK(intent.frame_coordinate==Catch::Approx(frame).margin(1e-9));
            scene.first_person_entities=made.projection->materialize_viewmodel(intent);REQUIRE(scene.first_person_entities);
            if(different_pixels(draw(scene).rgba8,empty.rgba8))++visible_frames;
        }
        std::cout<<"hand-grenade-viewmodel sequence="<<sequence<<" visible-sampled-frames="<<visible_frames<<'\n';
        CHECK(visible_frames>0);
    }
    CHECK(host.drain_remote_audio().count==0);CHECK(host.drain_remote_effects(.1).count==0);
    const auto player=made.projection->studio_model_data(7),weapon=made.projection->studio_model_data(71);
    REQUIRE(player);REQUIRE(weapon);
    c::RuntimePacketEntityObservation remote;remote.entity_number=2;remote.ordinary_visual_schema=remote.player_movement_schema=true;
    remote.model_index=7;remote.weapon_model_index=71;remote.origin={8192,8192,8192};remote.angles={0,0,0};
    remote.sequence=0;remote.frame=0;remote.animation_time_seconds=1;remote.frame_rate=1;remote.body=remote.skin=remote.gait_sequence=0;
    remote.controllers.fill(127);remote.blending.fill(127);s.packet_entities={remote};f::stamp(s,10,1);
    REQUIRE(made.projection->project_entities(s,world));world.set_camera({{8288,8128,8228},{8192,8192,8196},{0,0,1}});
    REQUIRE(made.projection->present_entities(host,10,1,world,{320,240}));
    REQUIRE(made.projection->present_entities(host,10.21,1,world,{320,240}));
    const auto frame=world.entity_frame();REQUIRE(frame);REQUIRE(frame->studio_instances().size()==2);
    const auto& parent=frame->studio_instances()[0];const auto& child=frame->studio_instances()[1];
    CHECK(parent.entity_number==2);CHECK(child.entity_number==2);CHECK(child.subinstance_ordinal==1);
    const auto& parent_pose=frame->studio_poses()[parent.pose_index].bone_matrices;
    const auto& child_pose=frame->studio_poses()[child.pose_index].bone_matrices;
    REQUIRE(parent_pose.size()==player->bones.size());REQUIRE(child_pose.size()==weapon->bones.size());std::size_t matched{};
    for(std::size_t i=0;i<weapon->bones.size();++i) {
        CHECK(std::ranges::all_of(child_pose[i],[](float v){return std::isfinite(v);}));
        const auto bone=std::ranges::find(player->bones,weapon->bones[i].name,&hlclient::assets::ModelBone::name);
        if(bone!=player->bones.end()){++matched;CHECK(child_pose[i]==parent_pose[static_cast<std::size_t>(bone-player->bones.begin())]);}
    }
    REQUIRE(matched>0);std::cout<<"hand-grenade-pmodel merged-bones="<<matched<<" child-bones="<<child_pose.size()<<'\n';
    scene=c::build_render_scene(world);scene.static_world.reset();scene.clear_color={0,0,0,1};const auto full=draw(scene);REQUIRE(full.non_clear_pixel_count>0);
    hlclient::entity_render::EntityRenderFrameBuildInput body;body.resource_id=1;body.resource_revision=1;body.interpolation=frame->interpolation();
    body.studio_instances.push_back(parent);body.studio_poses.assign(frame->studio_poses().begin(),frame->studio_poses().end());
    auto built=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*world.entity_scene(),std::move(body));REQUIRE(built);
    auto body_only=scene;body_only.dynamic_entities->frame=std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*built.frame));
    CHECK(different_pixels(full.rgba8,draw(body_only).rgba8));
    // No fire event is configured or injected for the remote hand grenade.
    CHECK(host.drain_remote_audio().count==0);CHECK(host.drain_remote_effects(10.21).count==0);CHECK(host.drain_follow_events().count==0);
    auto grenade=f::state().packet_entities.front();grenade.origin={8192,8192,8192};grenade.sequence=3;grenade.frame=0;
    s.packet_entities={grenade};f::stamp(s,11,1.2);REQUIRE(made.projection->project_entities(s,world));
    REQUIRE(world.entity_frame()->studio_instances().size()==1);
    scene={};scene.clear_color={0,0,0,1};scene.camera={{8224,8160,8212},{8192,8192,8192},{0,0,1},1.04719755F,.1F,128};
    const auto projectile_baseline=draw(scene);scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    const auto projectile=draw(scene);CHECK(different_pixels(projectile.rgba8,projectile_baseline.rgba8));CHECK(projectile.non_clear_pixel_count>0);
    const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;CHECK_FALSE(different_pixels(draw(scene).rgba8,projectile.rgba8));
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    s.packet_entities[0].angles={40,60,20};s.packet_entities[0].sequence=6;s.packet_entities[0].frame=128;
    f::stamp(s,12,1.3);REQUIRE(made.projection->project_entities(s,world));scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    CHECK(different_pixels(draw(scene).rgba8,projectile.rgba8));
    s.packet_entities[0].model_index=0;s.packet_entities[0].effects=128;f::stamp(s,13,1.4);REQUIRE(made.projection->project_entities(s,world));
    REQUIRE(world.entity_frame()->studio_instances().empty());scene.dynamic_entities.reset();CHECK_FALSE(different_pixels(draw(scene).rgba8,projectile_baseline.rgba8));
    hlclient::app::TemporaryEffectPresentation effects;effects.reset(1);
    for(const bool smoke:{false,true}) {
        const double now=smoke ? .3 : 0;auto event=f::effect(smoke ? 21 : 20,now,smoke);event.origin={8192,8192,8192};
        host.committed_temporary_effect(event);const auto batch=host.drain_temporary_effects(now);REQUIRE(batch.count==1);effects.consume(batch,now);
        auto image=scene;effects.present(image,now);image.transient_sprite_entities=made.projection->materialize_temporary_sprites(effects.sprites(),now);
        REQUIRE(image.transient_sprite_entities);CHECK(different_pixels(draw(image).rgba8,projectile_baseline.rgba8));
    }
    effects.present(scene,5);scene.transient_sprite_entities=made.projection->materialize_temporary_sprites(effects.sprites(),5);
    CHECK_FALSE(scene.transient_sprite_entities);CHECK_FALSE(different_pixels(draw(scene).rgba8,projectile_baseline.rgba8));
    gl->release_renderer();
}
