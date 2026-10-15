#include "satchel_test_fixture.hpp"
#include "hand_grenade_world_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdlib>
#include <iostream>
#include <thread>
TEST_CASE("E25 approved satchel charge radio pmodels and world model use normal import GL and PCM","[e25][satchel-assets][optional-local]") {
    namespace t=hlclient::tests;namespace sf=t::satchel_fixture;namespace wf=t::hand_grenade_world_fixture;
    namespace c=hlclient::client;namespace api=hlclient::game_api;namespace r=hlclient::renderer;
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* raw=nullptr;std::size_t count{};if(_wdupenv_s(&raw,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && raw)root=raw;std::free(raw);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal()) SKIP("Exact approved read-only research root opt-in absent");
    const auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0},
        {2,"models/v_satchel.mdl",75,0,0},{2,"models/v_satchel_radio.mdl",85,0,0},
        {2,"models/p_satchel.mdl",71,0,0},{2,"models/p_satchel_radio.mdl",81,0,0},
        {2,"models/w_satchel.mdl",237,0,0},{2,"models/grenade.mdl",312,0,0},{2,"models/player.mdl",7,0,0},
        {2,"sprites/zerogxplode.spr",319,0,0},{2,"sprites/steam1.spr",320,0,0},
        {0,"weapons/g_bounce1.wav",101,0,0},{0,"weapons/g_bounce2.wav",102,0,0},{0,"weapons/g_bounce3.wav",103,0,0}});
    auto made=hlclient::app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/crossfire.bsp"),
        root,"valve",hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(made.projection);
    c::ClientWorldState world;auto s=sf::state();wf::stamp(s,1,1);REQUIRE(made.projection->reset_generation(s,world));
    for(const unsigned slot:{75U,85U,71U,81U,237U}) {
        const auto model=made.projection->studio_model_data(slot);REQUIRE(model);
        std::cout<<"satchel-model slot="<<slot<<" bones="<<model->bones.size()<<" controllers="<<model->bone_controllers.size()
            <<" attachments="<<model->attachments.size()<<" bodies="<<model->bodyparts.size()<<" skins="<<model->skin_families.size()<<'\n';
        for(std::size_t i=0;i<model->sequences.size();++i)std::cout<<"satchel-sequence "<<slot<<" "<<i<<" "<<model->sequences[i].label
            <<" frames="<<model->sequences[i].frame_count<<" fps="<<model->sequences[i].frames_per_second
            <<" events="<<model->sequences[i].events.size()<<'\n';
        hlclient::assets::AssetVector3 lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};
        for(const auto& sub:model->submodels)for(const auto& vertex:sub.vertices) {
            const auto p=vertex.source_position;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
            hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
        }
        std::cout<<"satchel-bounds "<<slot<<" "<<lo.x<<","<<lo.y<<","<<lo.z<<" "<<hi.x<<","<<hi.y<<","<<hi.z<<'\n';
    }
    hlclient::goldsrc::ApprovedSoundAssets sounds(made.projection->sound_resources());
    for(const unsigned slot:{101U,102U,103U}) {
        auto loaded=sounds.request(static_cast<std::uint16_t>(slot));
        for(unsigned retry=0;loaded.status==hlclient::goldsrc::SoundAssetStatus::pending && retry<1000;++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});loaded=sounds.request(static_cast<std::uint16_t>(slot));
        }
        REQUIRE(loaded.status==hlclient::goldsrc::SoundAssetStatus::ready);REQUIRE(loaded.asset);
        double peak{};for(const auto value:loaded.asset->interleaved_samples){CHECK(std::isfinite(value));peak=std::max(peak,double(std::abs(value)));}
        CHECK(peak>.001);std::cout<<"satchel-pcm slot="<<slot<<" rate="<<loaded.asset->sample_rate<<" channels="<<loaded.asset->channel_count
            <<" samples="<<loaded.asset->interleaved_samples.size()<<" peak="<<peak<<'\n';
        wf::Sink sink;hlclient::goldsrc::ServerAudio audio(sink);audio.present({},{{},{0,-1,0},1,false},{},1);
        auto event=wf::sound(slot,40,slot,-50);event.sound.volume=255;audio.consume(event,{});audio.update(&sounds,{});REQUIRE(sink.starts.size()==1);
        std::array<float,2048> pcm{};sink.output.mixer.render(pcm);CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
        CHECK(std::ranges::any_of(pcm,[](float v){return std::abs(v)>.00001F;}));
    }
    const auto charge=made.projection->presentation_model(1,75),radio=made.projection->presentation_model(1,85);REQUIRE(charge);REQUIRE(radio);
    REQUIRE(hlclient::games::halflife::satchel_sequences(*charge));REQUIRE(hlclient::games::halflife::satchel_sequences(*radio));
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});host.set_movement_audio_time_origin(0);
    const std::array variants{*charge,*radio};REQUIRE(host.bind_model_variants(variants));host.bind_model(charge);host.observe(s,0);
    auto gl=t::entity_opengl_fixture::try_context(320,240);if(!gl || !t::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");
    gl->initialize_renderer();r::RenderScene scene;scene.camera={{100,80,80},{0,80,80},{0,0,1},1.04719755F,.1F,4096};
    const auto draw=[&](){gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);};
    const auto baseline=draw();
    std::size_t ordinal=1;
    for(const bool is_radio:{false,true})for(const unsigned seq:is_radio ? std::array{2U,3U,4U} : std::array{2U,3U,0U}) {
        const auto& model=is_radio ? *radio : *charge;auto state=sf::state(3,is_radio ? 1 : 0,++ordinal);
        state.weapon_slots[0].auxiliary_integer1_updated_in_record=true;
        const double start=static_cast<double>(ordinal)*10.0;host.bind_model(is_radio ? radio : charge);host.observe(state,start);
        std::size_t visible{};
        for(std::uint32_t frame=0;frame<model.sequences[seq].frame_count;frame+=3) {
            const double now=start+frame/model.sequences[seq].fps;
            const auto pose=host.viewmodel(state,is_radio ? radio : charge,now,api::LocalWeaponVisual{seq,0,100+seq+unsigned(is_radio)*10,start});
            REQUIRE(pose.status==api::ViewmodelStatus::ready);CHECK(pose.model_index==model.model_index);
            scene.first_person_entities=made.projection->materialize_viewmodel(pose);REQUIRE(scene.first_person_entities);
            const auto image=draw();visible+=image.rgba8!=baseline.rgba8;
            for(std::size_t row=0;row<4;++row) {
                std::size_t top_changed{};for(std::size_t x=0;x<320;++x) {
                    const auto p=(row*320+x)*4;
                    top_changed+=image.rgba8[p]!=baseline.rgba8[p] || image.rgba8[p+1]!=baseline.rgba8[p+1] || image.rgba8[p+2]!=baseline.rgba8[p+2];
                }
                CHECK(top_changed<240); // Reject the old full-width near-camera strip.
            }
        }
        std::cout<<"satchel-visible role="<<is_radio<<" sequence="<<seq<<" frames="<<visible<<'\n';CHECK(visible>0);
    }
    // Real third-person model pairs use server weaponmodel and generic bone merge.
    const auto player=made.projection->studio_model_data(7);REQUIRE(player);
    c::RuntimePacketEntityObservation remote;remote.entity_number=2;
    remote.ordinary_visual_schema=remote.player_movement_schema=true;remote.model_index=7;
    remote.origin={8192,8192,8192};remote.angles={0,0,0};remote.sequence=0U;remote.frame=0.0;
    remote.animation_time_seconds=1;remote.frame_rate=1;remote.body=remote.skin=remote.gait_sequence=0;
    remote.controllers.fill(127);remote.blending.fill(127);
    world.set_camera({{8288,8128,8228},{8192,8192,8196},{0,0,1}});
    for(const unsigned slot:{71U,81U,71U,0U}) {
        remote.weapon_model_index=slot;s.packet_entities={remote};++ordinal;wf::stamp(s,ordinal,1+static_cast<double>(ordinal)*.2);
        REQUIRE(made.projection->project_entities(s,world));const double at=100+static_cast<double>(ordinal)*.3;
        REQUIRE(made.projection->present_entities(host,at,1,world,{320,240}));
        REQUIRE(made.projection->present_entities(host,at+.21,1,world,{320,240}));
        const auto frame=world.entity_frame();REQUIRE(frame);REQUIRE(frame->studio_instances().size()==(slot ? 2U : 1U));
        if(!slot)continue;
        const auto weapon=made.projection->studio_model_data(slot);REQUIRE(weapon);
        const auto& parent=frame->studio_instances()[0];const auto& child=frame->studio_instances()[1];
        const auto& parent_pose=frame->studio_poses()[parent.pose_index].bone_matrices;
        const auto& child_pose=frame->studio_poses()[child.pose_index].bone_matrices;
        REQUIRE(child_pose.size()==weapon->bones.size());std::size_t merged{};
        for(std::size_t i=0;i<weapon->bones.size();++i) {
            CHECK(std::ranges::all_of(child_pose[i],[](float v){return std::isfinite(v);}));
            const auto bone=std::ranges::find(player->bones,weapon->bones[i].name,&hlclient::assets::ModelBone::name);
            if(bone!=player->bones.end()) {++merged;CHECK(child_pose[i]==parent_pose[static_cast<std::size_t>(bone-player->bones.begin())]);}
        }
        REQUIRE(merged>0);std::cout<<"satchel-pmodel slot="<<slot<<" merged="<<merged<<" child-bones="<<child_pose.size()<<'\n';
        scene=c::build_render_scene(world);scene.static_world.reset();scene.clear_color={0,0,0,1};
        const auto full=draw();REQUIRE(full.non_clear_pixel_count>0);
        hlclient::entity_render::EntityRenderFrameBuildInput body;body.resource_id=1;body.resource_revision=1;body.interpolation=frame->interpolation();
        body.studio_instances.push_back(parent);body.studio_poses.assign(frame->studio_poses().begin(),frame->studio_poses().end());
        auto built=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*world.entity_scene(),std::move(body));REQUIRE(built);
        scene.dynamic_entities->frame=std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*built.frame));
        const bool contribution=draw().rgba8!=full.rgba8;CHECK(contribution);
        CHECK(host.drain_remote_effects(at+.21).count==0);CHECK(host.drain_remote_audio().count==0);
    }
    gl->release_renderer();
}
