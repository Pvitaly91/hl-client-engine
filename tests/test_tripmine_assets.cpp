#include "tripmine_test_fixture.hpp"
#include "hand_grenade_world_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/goldsrc/sound_assets.hpp>
#include <cstdlib>
#include <iostream>
#include <thread>
TEST_CASE("E26 approved tripmine held world body pmodel laser and server samples use normal import GL and PCM","[e26][tripmine-assets][optional-local]") {
    namespace t=hlclient::tests;namespace f=t::tripmine_fixture;namespace wf=t::hand_grenade_world_fixture;
    namespace c=hlclient::client;namespace g=hlclient::goldsrc;namespace r=hlclient::renderer;
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* raw=nullptr;std::size_t count{};
    if(_wdupenv_s(&raw,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && raw)root=raw;std::free(raw);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("Exact approved read-only research root opt-in absent");
    const auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0},
        {2,"models/v_tripmine.mdl",75,0,0},{2,"models/p_tripmine.mdl",71,0,0},
        {2,"sprites/laserbeam.spr",315,0,0},{0,"weapons/mine_deploy.wav",101,0,0},
        {0,"weapons/mine_charge.wav",102,0,0},{0,"weapons/mine_activate.wav",103,0,0}});
    auto made=hlclient::app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/crossfire.bsp"),
        root,"valve",hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(made.projection);
    auto s=f::state();wf::stamp(s,1,1);c::ClientWorldState world;REQUIRE(made.projection->reset_generation(s,world));
    for(unsigned slot:{75U,71U}) {
        const auto data=made.projection->studio_model_data(slot);REQUIRE(data);
        std::cout<<"tripmine-model slot="<<slot<<" bones="<<data->bones.size()<<" attachments="<<data->attachments.size()
            <<" controllers="<<data->bone_controllers.size()<<" bodies="<<data->bodyparts.size()<<" skins="<<data->skin_families.size()<<'\n';
        for(unsigned i=0;i<data->sequences.size();++i)std::cout<<"tripmine-sequence "<<slot<<" "<<i<<" "<<data->sequences[i].label
            <<" frames="<<data->sequences[i].frame_count<<" fps="<<data->sequences[i].frames_per_second<<'\n';
    }
    auto metadata=made.projection->presentation_model(1,75);REQUIRE(metadata);
    for(unsigned body=0;body<4;++body)std::cout<<"tripmine-body "<<body<<" supported="<<metadata->supported_bodies[body]
        <<" selectable="<<metadata->selectable_bodies[body]<<'\n';
    CHECK(hlclient::games::halflife::tripmine_sequences(*metadata));
    g::ApprovedSoundAssets sounds(made.projection->sound_resources());
    for(unsigned slot:{101U,102U,103U}) {
        auto loaded=sounds.request(static_cast<std::uint16_t>(slot));
        for(unsigned retry=0;loaded.status==g::SoundAssetStatus::pending && retry<1000;++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});loaded=sounds.request(static_cast<std::uint16_t>(slot));
        }
        REQUIRE(loaded.status==g::SoundAssetStatus::ready);REQUIRE(loaded.asset);
        double peak{};for(auto v:loaded.asset->interleaved_samples){CHECK(std::isfinite(v));peak=std::max(peak,double(std::abs(v)));}
        CHECK(peak>.001);std::cout<<"tripmine-pcm "<<slot<<" rate="<<loaded.asset->sample_rate<<" channels="<<loaded.asset->channel_count
            <<" samples="<<loaded.asset->interleaved_samples.size()<<" peak="<<peak<<'\n';
        wf::Sink sink;g::ServerAudio audio(sink);audio.present({},{{},{0,-1,0},1,false},{},1);
        auto sound=wf::sound(slot,40,slot,-20);sound.sound.volume=255;sound.sound.pitch=static_cast<std::uint8_t>(slot==103 ? 75U : 100U);
        audio.consume(sound,{});audio.update(&sounds,{});REQUIRE(sink.starts.size()==1);
        std::array<float,4096> pcm{};sink.output.mixer.render(pcm);
        CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
        CHECK(std::ranges::any_of(pcm,[](float v){return std::abs(v)>.0001F;}));
    }
    auto gl=t::entity_opengl_fixture::try_context();if(!gl)SKIP("SDL/OpenGL output unavailable after asset/PCM checks");
    REQUIRE(t::entity_opengl_fixture::capable_context());gl->initialize_renderer();
    c::RuntimePacketEntityObservation mine;mine.entity_number=40;mine.model_index=75;mine.body=3;mine.sequence=7;
    mine.frame=0;mine.frame_rate=0;mine.animation_time_seconds=1;mine.origin={0.,0.,0.};mine.angles={0.,0.,0.};
    mine.ordinary_visual_schema=true;mine.solid=0;mine.brush_move_type=5;mine.effects=0;s.packet_entities={mine};wf::stamp(s,2,2);
    wf::project(*made.projection,s,world);f::Module module;REQUIRE(made.projection->present_entities(module.host,2,1,world,{320,240}));
    REQUIRE(world.entity_frame());REQUIRE(world.entity_frame()->studio_instances().size()==1);
    CHECK(world.entity_frame()->studio_instances()[0].body_value==3);
    r::RenderScene scene;scene.clear_color={0,0,0,1};scene.camera={{40,-50,24},{0,0,0},{0,0,1},1.047F,.1F,256};
    gl->renderer().render(scene,{96,96});const auto baseline=t::entity_opengl_fixture::framebuffer();
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    gl->renderer().render(scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);CHECK(t::entity_opengl_fixture::framebuffer()!=baseline);
}
