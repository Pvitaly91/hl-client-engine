#include "crossbow_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/games/halflife/egon.hpp>
#include <hlclient/games/halflife/server_messages.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <fstream>
#include "goldsrc_studio_test_fixture.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"
#include "python_357_pmodel_fixture.hpp"
#include <hlclient/entity_render/beam_ribbon.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <thread>
namespace egf {
namespace api=hlclient::game_api;namespace c=hlclient::client;namespace f=hlclient::tests::crossbow_fixture;
api::LocalWeaponModelMetadata model(){auto m=f::model();m.resource_name="models/v_egon.mdl";m.sequences.clear();m.supported_bodies[1]=m.selectable_bodies[1]=true;
    for(const auto name:{"idle1","fidget1","altfireon","altfirecycle","altfireoff","fire1","fire2","fire3","fire4","draw","holster"})m.sequences.push_back({30,31,true,{},name});
    m.sequences[5].frame_count=m.sequences[6].frame_count=1;return m;}
c::RuntimeClientObservationState state(int ammo=100){auto s=f::state(-1,1,ammo);s.weapon_hud.active_weapon_id=10;s.weapon_hud.clips[10]=-1;
    auto& t=s.weapon_hud.catalogue[0];t.id=10;t.command_name="weapon_egon";t.slot=3;t.position=2;
    s.receiving_client->owned_weapon_bits=1U<<10;s.weapon_slots[0].weapon_id=10;s.weapon_slots[0].auxiliary_integer3=0;return s;}
struct Module{api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};c::RuntimeClientObservationState s=state();
    Module(){host.reset({1,1,1});host.set_movement_audio_time_origin(0);host.bind_model(model());host.observe(s,0);std::array<api::ScriptedEventBinding,2> b{};b[0].index=777;b[1].index=778;
        constexpr std::string_view fire="events/egon_fire.sc",stop="events/egon_stop.sc";std::copy(fire.begin(),fire.end(),b[0].name.begin());std::copy(stop.begin(),stop.end(),b[1].name.begin());host.configure_scripted_events(b);}
    void command(unsigned seq,double at,unsigned buttons){(void)host.command_movement_effect(f::command(seq,at,static_cast<std::uint16_t>(buttons)));}
    void confirm(double at=.1){s.publication_revision++;s.weapon_slots[0].auxiliary_integer3=1;host.observe(s,at);}
};
struct World:api::IStaticEffectTrace {mutable hlclient::assets::AssetVector3 end{};api::StaticEffectTrace trace(hlclient::assets::AssetVector3,hlclient::assets::AssetVector3 b)const noexcept override{end=b;return {true,false,false,false,false,b,{}};}};
}
TEST_CASE("E22 primary WIDE shares uranium and one persistent lifecycle through normal host","[e22][no-stock][game-api]"){
    egf::Module m;m.command(1,.02,1);auto p=m.host.sample(.02);REQUIRE(p.identity);CHECK(p.identity->weapon_id==10);CHECK(p.actions_started==1);CHECK_FALSE(m.host.hud(m.s,.02).clip);CHECK(m.host.hud(m.s,.02).primary_reserve==100);
    auto audio=m.host.drain_audio();REQUIRE(audio.control_count==1);CHECK_FALSE(audio.controls[0].repeat_sample);CHECK(audio.controls[0].reference.name()=="weapons/egon_windup2.wav");
    m.confirm();for(unsigned i=2;i<=1500;++i){m.command(i,i*.02,1);(void)m.host.sample(i*.02);auto batch=m.host.drain_audio();if(i==101){REQUIRE(batch.control_count==1);CHECK(batch.controls[0].operation==egf::api::LocalVoiceControl::Operation::replace);CHECK(batch.controls[0].repeat_sample);}else CHECK(batch.control_count==0);}
    CHECK(m.host.hud(m.s,30).primary_reserve==100);CHECK(m.host.sample(30).actions_started==1);egf::World w;
    (void)m.host.local_visuals({{10,20,30},{0,1,0},{1,0,0},{0,0,1},{},30});auto beams=m.host.beam_effects(w,30,hlclient::assets::AssetVector3{11,22,33});REQUIRE(beams.count==2);CHECK(beams.sprites[0].persistent);CHECK(w.end.x==10);CHECK(w.end.y==2068);
    m.command(1501,30.02,0);auto stop=m.host.drain_audio();REQUIRE(stop.control_count==1);CHECK(stop.count==1);CHECK(m.host.beam_effects(w,30.02,{}).count==0);m.command(1501,30.02,0);CHECK(m.host.drain_audio().control_count==0);
}
TEST_CASE("E22 queued quantized command is not stale merely because observe render clock is ahead","[e22][e22-clock-fix][no-stock][composition]"){
    for(const bool reference:{false,true}) {
        CAPTURE(reference);egf::Module m;m.s.publication_revision=2;
        m.host.observe(m.s,.037); // actual app observes at wall time before draining .020 tick
        const auto command=egf::f::command(1,.020,1);
        if(reference)(void)m.host.command_movement_effect(command);else m.host.submit(command,.037);
        const auto shot=m.host.sample(.037);REQUIRE(shot.identity);CHECK(shot.actions_started==1);CHECK(shot.identity->started_at_seconds==.020);
        auto audio=m.host.drain_audio();REQUIRE(audio.control_count==1);CHECK(audio.controls[0].reference.name()=="weapons/egon_windup2.wav");CHECK(audio.controls[0].scheduled_seconds==.020);
        egf::World w;(void)m.host.local_visuals({{0,0,32},{0,1,0},{1,0,0},{0,0,1},{},.037});CHECK(m.host.beam_effects(w,.037,hlclient::assets::AssetVector3{0,0,30}).count==2);
        m.s.publication_revision=3;m.s.weapon_slots[0].auxiliary_integer3=1;m.host.observe(m.s,.057);
        const auto held=egf::f::command(2,.040,1);(void)m.host.command_movement_effect(held);m.host.submit(held,.057);CHECK(m.host.sample(.057).actions_started==1);CHECK(m.host.drain_audio().control_count==0);
        m.host.observe(m.s,.079);m.host.submit(egf::f::command(3,.060,0),.079);CHECK(m.host.sample(.079).visual->sequence==0);
        auto stop=m.host.drain_audio();CHECK(stop.control_count==1);CHECK(stop.count==1);CHECK(stop.controls[0].scheduled_seconds==.060);
        m.host.submit(egf::f::command(4,.030,1),.079);CHECK(m.host.sample(.079).actions_started==1);CHECK(m.host.drain_audio().control_count==0);
    }
}
TEST_CASE("E22 separate command clock preserves nonzero server timer and missing-field eligibility","[e22][e22-clock-fix][no-stock]"){
    egf::Module m;m.s.publication_revision=2;m.s.weapon_slots[0].next_primary_attack=.1;m.host.observe(m.s,.037);
    m.command(1,.020,1);CHECK(m.host.sample(.037).actions_started==0);CHECK(m.host.drain_audio().control_count==0);
    m.host.observe(m.s,.157);m.command(2,.140,1);CHECK(m.host.sample(.157).actions_started==1);
    egf::Module missing;missing.s.publication_revision=2;missing.s.weapon_slots[0].next_primary_attack.reset();missing.host.observe(missing.s,.037);missing.command(1,.020,1);
    CHECK(missing.host.sample(.037).actions_started==0);CHECK(missing.host.drain_audio().control_count==0);
}
TEST_CASE("E22 ScreenShake strict typed fixed point body and atomic failure","[e22][no-stock][user-message]"){
    auto s=egf::state();const std::array body{std::byte{0},std::byte{0x50},std::byte{0},std::byte{0x0c},std::byte{0},std::byte{0x96}};
    egf::api::GameMessageView view{egf::api::GameMessageKind::user_message,"ScreenShake",body,{1,1,0,0,48}};
    auto result=hlclient::games::halflife::stage_record({nullptr,s,{&view,1},1,8},nullptr);REQUIRE(result);REQUIRE(result.state->screen_shakes.size()==1);
    CHECK(result.state->screen_shakes[0].amplitude==5);CHECK(result.state->screen_shakes[0].duration_seconds==.75);CHECK(result.state->screen_shakes[0].frequency_hz==150);
    for(unsigned n=0;n<6;++n){view.body=std::span{body}.first(n);CHECK_FALSE(hlclient::games::halflife::stage_record({nullptr,s,{&view,1},1,8},nullptr));}
}
TEST_CASE("E22 normal viewmodel intent accepts primary fire body and single-frame variants","[e22][no-stock][viewmodel][a1]"){
    for(const unsigned command:{1U,3U,4U}){egf::Module m;auto model=egf::model();model.sequences[5].frame_count=model.sequences[6].frame_count=1;m.host.bind_model(model);m.host.observe(m.s,0);m.command(command,.02,1);
        auto shot=m.host.sample(.03);REQUIRE(shot.visual);const auto pose=m.host.viewmodel(m.s,model,.03,shot.visual);CHECK(pose.status==egf::api::ViewmodelStatus::ready);CHECK(pose.sequence==shot.visual->sequence);CHECK(pose.body==1);
        m.command(command+1,.04,0);CHECK(m.host.sample(.04).visual->sequence==0);
        m.s.publication_revision++;m.s.weapon_hud.animation_sequence=10;m.s.weapon_hud.animation_source=egf::c::RuntimeObservationSource{2,2,0,8,24};m.host.observe(m.s,.05);CHECK(m.host.sample(.05).visual->sequence==10);
        const auto restart=m.host.sample(.05).visual->restart_identity;m.host.observe(m.s,.06);CHECK(m.host.sample(.06).visual->restart_identity==restart);
    }
}
TEST_CASE("E22 old other-weapon service animation cannot leak across a new model bind","[e22][no-stock][lifecycle]"){
    egf::Module m;m.host.bind_model(egf::f::model());m.s.publication_revision=2;m.s.client_metadata.source=egf::c::RuntimeObservationSource{2,2,0,8,24};
    m.s.weapon_hud.animation_sequence=7;m.s.weapon_hud.animation_source=egf::c::RuntimeObservationSource{1,1,0,8,24};m.host.bind_model(egf::model());m.host.observe(m.s,.02);
    CHECK(m.host.sample(.02).visual->sequence==9);
}
TEST_CASE("E22 tap RMB rejection water depletion death holster and rejected prediction clean up","[e22][no-stock][lifecycle]"){
    for(unsigned reason=0;reason<7;++reason){egf::Module m;
        if(reason==0){m.command(1,.02,1U<<11);CHECK(m.host.sample(.02).actions_started==0);continue;}
        m.command(1,.02,1);(void)m.host.drain_audio();
        if(reason==1)m.command(2,.04,0);
        if(reason==2){m.s.receiving_client->water_level=3;m.s.publication_revision++;m.host.observe(m.s,.1);}
        if(reason==3){m.s.weapon_hud.reserve_ammo[1]=0;m.s.publication_revision++;m.host.observe(m.s,.1);}
        if(reason==4){m.s.receiving_client->health=0;m.s.publication_revision++;m.host.observe(m.s,.1);}
        if(reason==5)m.host.bind_model(egf::f::model());
        if(reason==6)(void)m.host.sample(.8);
        const auto stop=m.host.drain_audio();REQUIRE(stop.control_count==1);CHECK(stop.controls[0].operation==egf::api::LocalVoiceControl::Operation::stop);
        (void)m.host.sample(3);CHECK(m.host.drain_audio().control_count==0);egf::World w;CHECK(m.host.beam_effects(w,3,{}).count==0);
    }
}
TEST_CASE("E22 remote emitters independent assigned event IDs reliable stop dedup slot boundary","[e22][no-stock][remote-effects]"){
    egf::Module m;auto a=egf::f::event(false,2,2,.03);a.event_index=777;a.integer_parameter1=0;a.integer_parameter2=1;a.boolean_parameter1=true;
    m.host.committed_scripted_event(a);auto b=a;b.record=b.record_ordinal=3;b.emitter_entity=3;m.host.committed_scripted_event(b);
    auto starts=m.host.drain_audio();REQUIRE(starts.control_count==2);CHECK(starts.controls[0].identity!=starts.controls[1].identity);
    m.host.committed_scripted_event(a);CHECK(m.host.drain_audio().control_count==0);
    auto stop=a;stop.record=stop.record_ordinal=4;stop.event_index=778;stop.integer_parameter1=1;m.host.committed_scripted_event(stop);
    auto out=m.host.drain_audio();CHECK(out.control_count==1);CHECK(out.count==1);m.host.committed_scripted_event(stop);CHECK(m.host.drain_audio().count==0);
    m.host.remote_slot_boundary(3);CHECK(m.host.drain_audio().control_count==1);m.host.committed_scripted_event(b);CHECK(m.host.drain_audio().control_count==0);
}
TEST_CASE("E22 render FPS does not restart firing lifecycle and noise samples are deterministic","[e22][no-stock][prediction]"){
    for(const double fps:{30.,60.,144.,240.}){egf::Module m;m.command(1,.02,1);m.confirm(.021);const auto original=m.host.sample(.021).visual->restart_identity;(void)m.host.drain_audio();
        double render=.021;unsigned replacements=0;for(unsigned i=2;i<=1500;++i){m.command(i,i*.02,1);while(render+1/fps<=i*.02){render+=1/fps;(void)m.host.sample(render);}
            for(const auto batch=m.host.drain_audio();const auto& c:std::span{batch.controls}.first(batch.control_count))replacements+=c.operation==egf::api::LocalVoiceControl::Operation::replace;}
        CHECK(m.host.sample(30).actions_started==1);CHECK(m.host.sample(30).visual->restart_identity==original);CHECK(replacements==1);}
    const auto a=hlclient::entity_render::beam_ribbon_points({0,0,0},{100,0,0},2,true,1.5,7);
    const auto b=hlclient::entity_render::beam_ribbon_points({0,0,0},{100,0,0},2,true,1.5,7);
    const auto next=hlclient::entity_render::beam_ribbon_points({0,0,0},{100,0,0},2,true,1.6,7);
    CHECK(a.front().x==0);CHECK(a.back().x==100);CHECK(a[15].y==b[15].y);CHECK(a[15].y!=next[15].y);
}
namespace egon_assets {
namespace t=hlclient::tests;namespace api=hlclient::game_api;namespace g=hlclient::goldsrc;
std::vector<std::byte> mdl(){auto bytes=t::literal_minimal_goldsrc_studio_v10();const auto seqs=bytes.size();const auto meta=egf::model();bytes.resize(seqs+meta.sequences.size()*188);
    t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset,16,-4,-5);t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset+12,16,4,-5);t::studio_write_vector3(bytes,t::kSyntheticStudioVerticesOffset+24,16,0,3);
    t::studio_write_i32le(bytes,164,static_cast<int>(meta.sequences.size()));t::studio_write_i32le(bytes,168,static_cast<int>(seqs));
    for(std::size_t i=0;i<meta.sequences.size();++i){const auto at=seqs+i*176,anim=seqs+meta.sequences.size()*176+i*12;const auto& seq=meta.sequences[i];
        std::copy_n(bytes.begin()+t::kSyntheticStudioSequenceOffset,176,bytes.begin()+at);t::studio_write_fixed_string(bytes,at,32,seq.label);t::studio_write_f32le(bytes,at+32,30);
        t::studio_write_i32le(bytes,at+36,1);t::studio_write_i32le(bytes,at+56,static_cast<int>(seq.frame_count));t::studio_write_i32le(bytes,at+124,static_cast<int>(anim));}
    t::studio_write_i32le(bytes,72,static_cast<int>(bytes.size()));return bytes;}
std::vector<std::byte> wav(){std::vector<std::byte> b;const auto text=[&](std::string_view s){for(char v:s)b.push_back(static_cast<std::byte>(v));};
    const auto number=[&](unsigned n,unsigned width=4){for(unsigned i=0;i<width;++i)b.push_back(static_cast<std::byte>((n>>(8*i))&255));};
    text("RIFF");number(4836);text("WAVEfmt ");number(16);number(1,2);number(1,2);number(22050);number(22050);number(1,2);number(8,2);text("data");number(4800);
    for(unsigned i=0;i<4800;++i)b.push_back(static_cast<std::byte>(i%32<16 ? 160 : 96));return b;}
struct Owned {t::ScopedLocalResourceTestRoot root;std::unique_ptr<hlclient::app::RuntimeReplayLocalAssets> assets;egf::c::ClientWorldState world;
    explicit Owned(bool transverse_profile=false){t::SyntheticBspBuilder bsp;const auto collision=t::literal_collision_goldsrc_bsp_v30();
        for(const auto id:{t::SyntheticBspLumpId::nodes,t::SyntheticBspLumpId::leaves,t::SyntheticBspLumpId::clipnodes,t::SyntheticBspLumpId::models}){
            const auto at=4+static_cast<std::size_t>(id)*8;const auto offset=t::synthetic_read_i32le(collision,at),size=t::synthetic_read_i32le(collision,at+4);bsp.lump(id).assign(collision.begin()+offset,collision.begin()+offset+size);}
        const std::string entities="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";const auto eb=std::as_bytes(std::span{entities});bsp.lump(t::SyntheticBspLumpId::entities).assign(eb.begin(),eb.end());
        t::SyntheticWad3Entry texture;texture.name="TEST_QUAD";texture.payload=t::synthetic_goldsrc_miptex("TEST_QUAD",64,64);root.write("valve","test.wad",t::synthetic_wad3({texture}).bytes);root.write("valve","maps/test.bsp",bsp.build());
        root.write("valve","models/v_egon.mdl",mdl());root.write("valve","models/owned_player.mdl",t::python_fixture::remote_model(false));
        auto p=t::python_fixture::remote_model(true);t::studio_write_i32le(p,140,10);
        t::studio_write_vector3(p,t::kSyntheticStudioVerticesOffset,-12.03F,-14.99F,-12.22F);
        t::studio_write_vector3(p,t::kSyntheticStudioVerticesOffset+12,29.07F,5.89F,-12.22F);
        t::studio_write_vector3(p,t::kSyntheticStudioVerticesOffset+24,-12.03F,0,10.08F);
        for(unsigned i=0;i<3;++i){p[t::kSyntheticStudioVertexBonesOffset+i]=p[t::kSyntheticStudioNormalBonesOffset+i]=std::byte{9};}root.write("valve","models/p_egon.mdl",p);
        auto sprite=t::sprite_fixture::literal_header_and_palette(1,1,2,0,8,8);for(unsigned i=0;i<256;++i){const auto at=t::sprite_fixture::kPaletteOffset+i*3;sprite[at]=std::byte{0};sprite[at+1]=std::byte{100};sprite[at+2]=std::byte{255};}
        t::sprite_fixture::FrameSpec strip{-4,4,8,8,std::vector<std::byte>(64,std::byte{1})};
        if(transverse_profile) {
            for(unsigned c=0;c<3;++c)sprite[t::sprite_fixture::kPaletteOffset+c]=std::byte{0};
            // A bright center in texture U, invariant along V: transposing the
            // axes produces a short broad rectangle, not a full-length thread.
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
                strip.pixels[y*8+x]=std::byte{static_cast<unsigned char>(x==3 || x==4)};
        }
        t::sprite_fixture::append_single_entry(sprite,strip);root.write("valve","sprites/xbeam1.spr",sprite);
        auto flare=t::sprite_fixture::literal_header_and_palette(2,1,2,0,8,8);t::sprite_fixture::append_single_entry(flare,strip);t::sprite_fixture::append_single_entry(flare,strip);root.write("valve","sprites/XSpark1.spr",flare);
        for(const auto& r:hlclient::games::halflife::EgonPresentation::resources())root.write("valve",std::string{"sound/"}+std::string{r.name()},wav());
        auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0},{2,"models/v_egon.mdl",73,0,0},{2,"models/p_egon.mdl",71,0,0},{2,"models/owned_player.mdl",7,0,0},{2,"sprites/xbeam1.spr",75,0,0},{2,"sprites/XSpark1.spr",76,0,0}});
        auto made=hlclient::app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/test.bsp"),root.path(),"valve",hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(made.projection);assets=std::move(made.projection);
        auto s=egf::state();s.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{1,1,0,0,8}};
        s.client_metadata=s.server_time_metadata=s.entity_metadata;s.server_time_seconds=0;s.canonical_state_hash=egf::c::runtime_observation_canonical_hash(s);REQUIRE(assets->reset_generation(s,world));}
};
void ready(g::ApprovedSoundAssets& loader){for(const auto& r:hlclient::games::halflife::EgonPresentation::resources()){auto s=loader.request_local(r);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{2};
    while(s.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<end){std::this_thread::sleep_for(std::chrono::milliseconds{1});s=loader.request_local(r);}INFO(r.name());REQUIRE(s.status==g::SoundAssetStatus::ready);REQUIRE(s.asset);CHECK(std::ranges::any_of(s.asset->interleaved_samples,[](float f){return std::isfinite(f)&&f!=0;}));}}
}
namespace egon_pixels {
void set_glow(hlclient::renderer::RenderScene& scene,float opacity) {
    REQUIRE(scene.follow_trail_entities);
    auto& entities=*scene.follow_trail_entities;const auto& original=*entities.frame;
    hlclient::entity_render::EntityRenderFrameBuildInput input;
    input.resource_id=original.resource_id();input.resource_revision=original.resource_revision()+1;
    input.interpolation=original.interpolation();
    input.sprite_instances.assign(original.sprite_instances().begin(),original.sprite_instances().end());
    for(auto& instance:input.sprite_instances)instance.ribbon_glow_opacity=opacity;
    auto frame=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*entities.package,std::move(input));REQUIRE(frame);
    entities.frame=std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*frame.frame));
}
void save(std::string_view name,const hlclient::renderer::opengl::OpenGlFramebufferObservation& image) {
#ifdef _WIN32
    // Optional fixture-only evidence; production rendering has no file paths.
    char* directory=nullptr;std::size_t length=0;
    if(_dupenv_s(&directory,&length,"HLCLIENT_EGON_BEAM_PIXEL_DIR")==0 && directory) {
        std::ofstream file(std::filesystem::path{directory}/name,std::ios::binary);
        REQUIRE(file);REQUIRE(image.rgba8.size()==320U*240U*4U);file<<"P6\n320 240\n255\n";
        for(int y=239;y>=0;--y)for(unsigned x=0;x<320;++x)
            file.write(reinterpret_cast<const char*>(image.rgba8.data()+(y*320+x)*4),3);
        REQUIRE(file.good());
    }
    std::free(directory);
#endif
}
}
TEST_CASE("E22 beam texture keeps its thin transverse profile during longitudinal scrolling",
          "[e22][e22-beam-uv][no-stock][opengl][composition]") {
    egon_assets::Owned owned(true);hlclient::game_api::BeamSpriteFrame beam;
    beam.action.generation=1;beam.action.sub_identity=1;beam.starts=.1;beam.expires=.2;beam.persistent=true;
    beam.start={0,0,32};beam.end={0,96,32};beam.width=8;beam.color={1,1,1};beam.opacity=1;beam.scroll_cycles_per_second=1;
    constexpr std::string_view name="sprites/xbeam1.spr";std::copy(name.begin(),name.end(),beam.virtual_sprite.begin());
    hlclient::renderer::RenderScene scene;scene.camera={{96,48,32},{0,48,32},{0,0,1},1.04719755F,.1F,8192};scene.clear_color={0,0,0,1};
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");
    gl->initialize_renderer();
    const auto capture=[&] {
        owned.assets->present_entity_effects(scene,.1,{&beam,1});REQUIRE(scene.follow_trail_entities);
        gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        auto pixels=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(pixels);return pixels;
    };
    const auto first=capture();egon_pixels::save("owned-beam-uv.ppm",first);
    for(const unsigned x:{80U,160U,240U}) {
        unsigned bright=0;for(unsigned y=0;y<240;++y)if(first.rgba8[(y*320+x)*4+2]>64)++bright;
        CAPTURE(x,bright);CHECK(bright>=3);CHECK(bright<=8);
    }
    beam.phase_seconds=.375;const auto scrolled=capture();CHECK(first.rgba8==scrolled.rgba8);
    owned.assets->present_entity_effects(scene,.3);gl->renderer().render(scene,{320,240});
    CHECK(gl->renderer().observe_framebuffer({320,240},scene.clear_color,true).non_clear_pixel_count==0);
    gl->release_renderer();
}
TEST_CASE("E22 approved owned resources produce isolated startup run and off PCM in normal audio sink","[e22][no-stock][audio][composition]"){
    egon_assets::Owned owned;hlclient::goldsrc::ApprovedSoundAssets loader(owned.assets->sound_resources());egon_assets::ready(loader);egf::Module m;
    hlclient::audio::OfflineOutput output;output.set_listener({{}, {0,-1,0},1,false});hlclient::goldsrc::LocalAudio delivery(output);
    const auto deliver=[&](egf::api::LocalAudioBatch b,double at,unsigned starts){delivery.update(b,&loader,at,true);b.count=b.control_count=b.prepare_count=0;
        for(unsigned i=1;i<=125 && output.mixer.statistics().started<starts;++i){std::this_thread::sleep_for(std::chrono::milliseconds{1});delivery.update(b,&loader,at+i*.001,true);}};
    m.host.observe(m.s,.037);m.command(1,.02,1);deliver(m.host.drain_audio(),.037,1);REQUIRE(output.mixer.active()==1);std::array<float,512> pcm{};output.mixer.render(pcm);CHECK(std::ranges::any_of(pcm,[](float f){return f!=0;}));
    m.confirm();(void)m.host.sample(2.03);deliver(m.host.drain_audio(),2.03,2);CHECK(output.mixer.statistics().started==2);CHECK(output.mixer.active()==1);output.mixer.render(pcm);CHECK(std::ranges::any_of(pcm,[](float f){return std::isfinite(f)&&f!=0;}));
    m.command(2,2.04,0);deliver(m.host.drain_audio(),2.04,3);CHECK(output.mixer.statistics().started==3);CHECK(output.mixer.active()==1);output.mixer.render(pcm);CHECK(std::ranges::any_of(pcm,[](float f){return f!=0;}));
    std::array<float,24000> finish{};output.mixer.render(finish);CHECK(output.mixer.active()==0);m.host.reset({2,2,1});delivery.update(m.host.drain_audio(),&loader,3,true);CHECK(output.mixer.active()==0);
}
TEST_CASE("E22 owned persistent beam normal scene GPU pose update and cleanup","[e22][no-stock][opengl][composition]"){
    egon_assets::Owned owned;egf::Module m;m.host.bind_model(owned.assets->presentation_model(1,73));m.host.observe(m.s,.037);m.command(1,.02,1);m.confirm();
    egf::World w;(void)m.host.local_visuals({{0,0,32},{0,1,0},{1,0,0},{0,0,1},{},.1});auto beam=m.host.beam_effects(w,.1,hlclient::assets::AssetVector3{0,0,30});REQUIRE(beam.count==2);
    hlclient::renderer::RenderScene scene;scene.camera={{96,-64,48},{0,40,30},{0,0,1},1.04719755F,.1F,8192};owned.assets->present_entity_effects(scene,.1,{beam.sprites.data(),beam.count});REQUIRE(scene.follow_trail_entities);CHECK(scene.follow_trail_entities->frame->sprite_instances().size()==2);
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");gl->initialize_renderer();
    auto blank=scene;blank.follow_trail_entities.reset();gl->renderer().render(blank,{320,240});const auto baseline=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);const auto first=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);CHECK(first.non_clear_pixel_count>0);CHECK_FALSE(std::ranges::equal(first.rgba8,baseline.rgba8));
    (void)m.host.local_visuals({{0,0,32},{1,0,0},{0,-1,0},{0,0,1},{},.2});beam=m.host.beam_effects(w,.2,hlclient::assets::AssetVector3{0,1,30});owned.assets->present_entity_effects(scene,.2,{beam.sprites.data(),beam.count});
    gl->renderer().render(scene,{320,240});CHECK_FALSE(std::ranges::equal(first.rgba8,gl->renderer().observe_framebuffer({320,240},scene.clear_color,true).rgba8));
    m.command(2,.21,0);beam=m.host.beam_effects(w,.21,{});owned.assets->present_entity_effects(scene,.21,{beam.sprites.data(),beam.count});CHECK_FALSE(scene.follow_trail_entities);gl->renderer().render(scene,{320,240});CHECK(std::ranges::equal(baseline.rgba8,gl->renderer().observe_framebuffer({320,240},scene.clear_color,true).rgba8));gl->release_renderer();
}
TEST_CASE("E22 local and server sine beams visibly wrap their core and rotate through normal OpenGL",
          "[e22][e22-twist][no-stock][opengl][composition]") {
    using V=hlclient::assets::AssetVector3;
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");
    gl->initialize_renderer();
    for(const bool server:{false,true}) {
        CAPTURE(server);egon_assets::Owned owned;egf::Module m;m.command(1,.02,1);m.confirm();
        struct Wall:egf::api::IStaticEffectTrace {
            egf::api::StaticEffectTrace trace(V,V)const noexcept override{return {true,false,false,true,false,{0,96,32},{0,-1,0}};}
        } wall;
        auto state=egf::state();state.packet_entities.clear();
        hlclient::renderer::RenderScene scene;
        scene.camera={{96,48,32},{0,48,32},{0,0,1},1.04719755F,.1F,8192};scene.clear_color={0,0,0,1};
        const auto compose=[&](bool twist,double at) {
            if(server) {
                state.packet_entities.clear();++state.publication_revision;
                const auto rev=state.publication_revision;
                state.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,
                    egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{rev,static_cast<std::size_t>(rev),0,0,8}};
                state.client_metadata=state.server_time_metadata=state.entity_metadata;state.server_time_seconds=at;
                for(unsigned i=0;i<2;++i) {
                    egf::c::RuntimePacketEntityObservation e;e.entity_number=3+i;e.origin={0.,96.,32.};e.angles={0.,0.,32.};
                    e.custom_beam.emplace();auto& b=*e.custom_beam;b.type=egf::c::RuntimeCustomBeamObservation::Type::points;
                    b.flags=static_cast<std::uint8_t>(i ? 0 : 16);b.sprite_model_index=75;b.width_units=i ? 5.5 : 4;
                    b.noise_raw=i ? 8 : (twist ? 20 : 0);b.scroll_rate=i ? 2.5 : 5;b.color={128,128,255};
                    b.opacity=178;b.complete=b.supported=true;state.packet_entities.push_back(e);
                }
                state.canonical_state_hash=egf::c::runtime_observation_canonical_hash(state);
                REQUIRE(owned.assets->project_entities(state,owned.world));
                REQUIRE(owned.assets->present_entities(m.host,at,1,owned.world,{320,240}));
                owned.assets->present_entity_effects(scene,at);
            } else {
                (void)m.host.local_visuals({{0,0,32},{0,1,0},{1,0,0},{0,0,1},{},at});
                auto beams=m.host.beam_effects(wall,at,V{0,0,32});REQUIRE(beams.count==2);
                if(!twist)beams.sprites[0].noise_amplitude=0;
                owned.assets->present_entity_effects(scene,at,{beams.sprites.data(),beams.count});
            }
            REQUIRE(scene.follow_trail_entities);
            // This pre-existing shape regression isolates the accepted core.
            // Halo visibility and depth have a separate spatial A/B below.
            egon_pixels::set_glow(scene,0);
            const auto instances=scene.follow_trail_entities->frame->sprite_instances();REQUIRE(instances.size()==2);
            CHECK(instances[0].ribbon_sine_noise);CHECK_FALSE(instances[1].ribbon_sine_noise);
            // Every centerline sample plus its ribbon half-width must fit the
            // materializer bounds, including a diagonal/rotating helix's peak.
            for(const auto& instance:instances) {
                const auto points=hlclient::entity_render::beam_ribbon_points(instance.transform.origin,*instance.ribbon_end,
                    instance.ribbon_noise_amplitude,instance.ribbon_sine_noise,at,instance.entity_number);
                const auto pad=instance.ribbon_width*.5F;
                for(const auto p:points) {
                    CHECK(p.x-pad>=instance.bounds.minimum.x);CHECK(p.x+pad<=instance.bounds.maximum.x);
                    CHECK(p.y-pad>=instance.bounds.minimum.y);CHECK(p.y+pad<=instance.bounds.maximum.y);
                    CHECK(p.z-pad>=instance.bounds.minimum.z);CHECK(p.z+pad<=instance.bounds.maximum.z);
                }
            }
        };
        const auto pixels=[&]() {gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
            auto image=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(image);return image;};
        compose(true,.1);const auto twisted=pixels();const auto repeated=pixels();CHECK(twisted.rgba8==repeated.rgba8);
        compose(false,.1);const auto straight=pixels();
        const auto outside_core=[](const auto& image) {
            unsigned count=0;for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)
                if((y<108 || y>=132) && image.rgba8[(y*320+x)*4+2]>8)++count;
            return count;
        };
        CHECK(outside_core(twisted)>80);CHECK(outside_core(straight)==0);
        compose(true,.25);CHECK(pixels().rgba8!=twisted.rgba8);
        egon_pixels::save(server ? "observer-twist.ppm" : "local-twist.ppm",twisted);
        if(server) {
            state.packet_entities.clear();++state.publication_revision;
            state.entity_metadata.source->record_identity=state.publication_revision;
            state.entity_metadata.source->record_ordinal=static_cast<std::size_t>(state.publication_revision);
            state.canonical_state_hash=egf::c::runtime_observation_canonical_hash(state);
            REQUIRE(owned.assets->project_entities(state,owned.world));REQUIRE(owned.assets->present_entities(m.host,.3,1,owned.world,{320,240}));
            owned.assets->present_entity_effects(scene,.3);
        } else {m.command(2,.3,0);owned.assets->present_entity_effects(scene,.3);}
        CHECK_FALSE(scene.follow_trail_entities);CHECK(pixels().non_clear_pixel_count==0);
    }
    gl->release_renderer();
}
TEST_CASE("E22 glow profile is bounded selected and does not invent another firing lifecycle",
          "[e22][e22-glow][no-stock][game-api]") {
    egf::Module m;const auto profile=m.host.beam_ribbon_glow_profile("sprites/xbeam1.spr");
    CHECK(profile.width_scale==4);CHECK(profile.opacity==Catch::Approx(.35F));
    for(const auto name:{"sprites/laserbeam.spr","sprites/smoke.spr","sprites/XSpark1.spr","models/p_egon.mdl",""})
        CHECK(m.host.beam_ribbon_glow_profile(name).opacity==0);
    for(const auto invalid:std::array<egf::api::BeamRibbonGlowProfile,6>{{
        {0,.5F},{9,.5F},{4,-.1F},{4,1.1F},
        {std::numeric_limits<float>::quiet_NaN(),.5F},{4,std::numeric_limits<float>::infinity()}}})
        CHECK_FALSE(egf::api::valid_beam_ribbon_glow(invalid));
    m.command(1,.02,1);m.confirm();egf::World world;
    const auto beams=m.host.beam_effects(world,.1,hlclient::assets::AssetVector3{0,0,30});REQUIRE(beams.count==2);
    for(unsigned i=0;i<2;++i) {
        const auto& b=beams.sprites[i];CHECK(b.ribbon_glow.width_scale==profile.width_scale);CHECK(b.ribbon_glow.opacity==profile.opacity);
        CHECK(b.width==(i ? 5.F : 3.5F));CHECK(b.noise_amplitude==(i ? .8F : 2.F));CHECK(b.sine_noise==!i);
    }
    m.command(2,.2,0);CHECK(m.host.beam_effects(world,.2,{}).count==0);
    m.host.teardown();CHECK(m.host.beam_ribbon_glow_profile("sprites/xbeam1.spr").opacity==0);
}
TEST_CASE("E22 malformed halo requests and renderer frames cannot publish light",
          "[e22][e22-glow][no-stock][composition]") {
    egon_assets::Owned owned(true);egf::Module m;m.command(1,.02,1);m.confirm();egf::World world;
    auto beams=m.host.beam_effects(world,.1,hlclient::assets::AssetVector3{0,0,30});REQUIRE(beams.count==2);
    auto beam=beams.sprites[0];hlclient::renderer::RenderScene scene;
    owned.assets->present_entity_effects(scene,.1,{&beam,1});REQUIRE(scene.follow_trail_entities);
    const auto valid=*scene.follow_trail_entities;
    for(unsigned reason=0;reason<6;++reason) {
        CAPTURE(reason);beam=beams.sprites[0];
        if(reason==0)beam.ribbon_glow.width_scale=std::numeric_limits<float>::quiet_NaN();
        if(reason==1)beam.ribbon_glow.opacity=std::numeric_limits<float>::infinity();
        if(reason==2)beam.ribbon_glow.width_scale=9;
        if(reason==3)beam.ribbon_glow.opacity=-.1F;
        if(reason==4)beam.ribbon=false;
        if(reason==5)beam.geometry=egf::api::WorldLineGeometry::solid_3d;
        owned.assets->present_entity_effects(scene,.1,{&beam,1});CHECK_FALSE(scene.follow_trail_entities);
    }
    for(unsigned reason=0;reason<6;++reason) {
        CAPTURE(reason);hlclient::entity_render::EntityRenderFrameBuildInput input;
        input.resource_id=valid.frame->resource_id();input.resource_revision=valid.frame->resource_revision()+1;input.interpolation=valid.frame->interpolation();
        input.sprite_instances.assign(valid.frame->sprite_instances().begin(),valid.frame->sprite_instances().end());
        auto& instance=input.sprite_instances[0];
        if(reason==0)instance.ribbon_glow_width_scale=std::numeric_limits<float>::quiet_NaN();
        if(reason==1)instance.ribbon_glow_opacity=std::numeric_limits<float>::infinity();
        if(reason==2)instance.ribbon_glow_width_scale=9;
        if(reason==3)instance.ribbon_glow_opacity=-.1F;
        if(reason==4)instance.ribbon_end.reset();
        if(reason==5)instance.blend_mode=hlclient::entity_render::SpriteBlendMode::opaque;
        CHECK_FALSE(hlclient::entity_render::EntityRenderFrameBuilder{}.build(*valid.package,std::move(input)));
    }
    beam=beams.sprites[0];beam.virtual_sprite.fill('\0');
    owned.assets->present_entity_effects(scene,.1,{&beam,1});CHECK_FALSE(scene.follow_trail_entities);
    beam=beams.sprites[0];beam.action.generation=2;
    owned.assets->present_entity_effects(scene,.1,{&beam,1});CHECK_FALSE(scene.follow_trail_entities);
}
TEST_CASE("E22 normal local and observer beam glow expands light only and obeys world depth",
          "[e22][e22-glow][no-stock][opengl][composition]") {
    using V=hlclient::assets::AssetVector3;
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");
    gl->initialize_renderer();
    for(const bool server:{false,true}) {
        CAPTURE(server);egon_assets::Owned owned(true);egf::Module m;m.command(1,.02,1);m.confirm();
        hlclient::renderer::RenderScene scene;scene.camera={{96,48,32},{0,48,32},{0,0,1},1.04719755F,.1F,8192};scene.clear_color={0,0,0,1};
        auto state=egf::state();state.packet_entities.clear();
        if(server) {
            ++state.publication_revision;const auto rev=state.publication_revision;
            state.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,
                egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{rev,static_cast<std::size_t>(rev),0,0,8}};
            state.client_metadata=state.server_time_metadata=state.entity_metadata;state.server_time_seconds=.1;
            for(unsigned i=0;i<2;++i) {
                egf::c::RuntimePacketEntityObservation e;e.entity_number=3+i;e.origin={0.,96.,32.};e.angles={0.,0.,32.};
                e.custom_beam.emplace();auto& b=*e.custom_beam;b.type=egf::c::RuntimeCustomBeamObservation::Type::points;
                b.flags=static_cast<std::uint8_t>(i ? 0 : 16);b.sprite_model_index=75;b.width_units=i ? 5 : 3.5;
                // This observation is already wire-decoded (bounded 0..25.5),
                // not the pre-decode byte value. Preserve strict validation.
                b.noise_raw=i ? 8 : 20;b.scroll_rate=i ? 2.5 : 5.5;b.color={128,128,255};b.opacity=178;b.complete=b.supported=true;
                state.packet_entities.push_back(e);
            }
            state.canonical_state_hash=egf::c::runtime_observation_canonical_hash(state);
            REQUIRE(egf::c::valid_runtime_observation(state));
            REQUIRE(owned.assets->project_entities(state,owned.world));REQUIRE(owned.assets->present_entities(m.host,.1,1,owned.world,{320,240}));
            owned.assets->present_entity_effects(scene,.1);
        } else {
            struct Wall:egf::api::IStaticEffectTrace {egf::api::StaticEffectTrace trace(V,V)const noexcept override{return {true,false,false,true,false,{0,96,32},{0,-1,0}};}} wall;
            (void)m.host.local_visuals({{0,0,32},{0,1,0},{1,0,0},{0,0,1},{},.1});
            const auto beams=m.host.beam_effects(wall,.1,V{0,0,32});REQUIRE(beams.count==2);
            owned.assets->present_entity_effects(scene,.1,{beams.sprites.data(),beams.count});
        }
        REQUIRE(scene.follow_trail_entities);const auto glowing=scene;
        const auto original=scene.follow_trail_entities->frame;
        for(const auto& instance:original->sprite_instances()) {
            CHECK(instance.ribbon_glow_width_scale==4);CHECK(instance.ribbon_glow_opacity==Catch::Approx(.35F));
            const auto points=hlclient::entity_render::beam_ribbon_points(instance.transform.origin,*instance.ribbon_end,
                instance.ribbon_noise_amplitude,instance.ribbon_sine_noise,instance.ribbon_phase_seconds,instance.entity_number);
            const auto radius=instance.ribbon_width*instance.ribbon_glow_width_scale*.5F;
            for(const auto p:points) {
                CHECK(p.x-radius>=instance.bounds.minimum.x);CHECK(p.x+radius<=instance.bounds.maximum.x);
                CHECK(p.y-radius>=instance.bounds.minimum.y);CHECK(p.y+radius<=instance.bounds.maximum.y);
                CHECK(p.z-radius>=instance.bounds.minimum.z);CHECK(p.z+radius<=instance.bounds.maximum.z);
            }
        }
        const auto pixels=[&] {gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
            auto result=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(result);return result;};
        const auto before_draws=gl->renderer().entity_statistics().sprite_draw_count;
        const auto glow=pixels();const auto glow_draws=gl->renderer().entity_statistics().sprite_draw_count-before_draws;
        CHECK(glow.rgba8==pixels().rgba8);
        const auto uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;
        const auto draws=gl->renderer().entity_statistics().sprite_draw_count;
        egon_pixels::set_glow(scene,0);CHECK(scene.follow_trail_entities->frame->frame_signature()!=original->frame_signature());
        const auto core=pixels();CHECK((gl->renderer().entity_statistics().sprite_draw_count-draws)*2==glow_draws);
        CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==uploads);
        const auto core_instances=scene.follow_trail_entities->frame->sprite_instances();
        for(unsigned i=0;i<2;++i) {
            const auto& a=original->sprite_instances()[i];const auto& b=core_instances[i];
            CHECK(a.entity_number==b.entity_number);CHECK(a.ribbon_width==b.ribbon_width);CHECK(a.ribbon_phase_seconds==b.ribbon_phase_seconds);
            const auto ap=hlclient::entity_render::beam_ribbon_points(a.transform.origin,*a.ribbon_end,a.ribbon_noise_amplitude,a.ribbon_sine_noise,a.ribbon_phase_seconds,a.entity_number);
            const auto bp=hlclient::entity_render::beam_ribbon_points(b.transform.origin,*b.ribbon_end,b.ribbon_noise_amplitude,b.ribbon_sine_noise,b.ribbon_phase_seconds,b.entity_number);
            for(unsigned n=0;n<ap.size();++n){CHECK(ap[n].x==bp[n].x);CHECK(ap[n].y==bp[n].y);CHECK(ap[n].z==bp[n].z);}
        }
        unsigned halo_pixels=0,core_preserved=0;
        for(std::size_t i=0;i<glow.rgba8.size();i+=4) {
            if(core.rgba8[i+2]<8 && glow.rgba8[i+2]>12)++halo_pixels;
            if(core.rgba8[i+2]>64){++core_preserved;CHECK(glow.rgba8[i+2]>=core.rgba8[i+2]);}
        }
        CAPTURE(halo_pixels,core_preserved);CHECK(halo_pixels>20);CHECK(core_preserved>20);
        std::cout<<"egon-glow server="<<server<<" halo_pixels="<<halo_pixels<<" core_pixels="<<core_preserved<<"\n";
        egon_pixels::save(server ? "observer-glow.ppm" : "local-glow.ppm",glow);
        namespace wf=hlclient::tests::world_render_fixture;
        auto wall=wf::make_world();for(auto& vertex:wall.vertices){vertex.position={48,-64+vertex.position.x*14,-96+vertex.position.y*16};vertex.normal={1,0,0};}
        wall.bounds={{48,-64,-96},{48,160,160}};wall.surfaces[0].bounds=wall.bounds;
        auto package=hlclient::world_render::WorldRenderPackageBuilder{}.build({std::move(wall),wf::make_texture_set(false)},wf::make_lightmap_set({}));REQUIRE(package);
        scene=glowing;scene.static_world=hlclient::renderer::RenderStaticWorld{
            std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*package.package))};
        const auto occluded=pixels();scene.follow_trail_entities.reset();const auto wall_only=pixels();CHECK(occluded.rgba8==wall_only.rgba8);
        if(server) {
            state.packet_entities.clear();++state.publication_revision;state.entity_metadata.source->record_identity=state.publication_revision;
            state.entity_metadata.source->record_ordinal=static_cast<std::size_t>(state.publication_revision);
            state.canonical_state_hash=egf::c::runtime_observation_canonical_hash(state);
            REQUIRE(owned.assets->project_entities(state,owned.world));REQUIRE(owned.assets->present_entities(m.host,.2,1,owned.world,{320,240}));
        } else m.command(2,.2,0);
        owned.assets->present_entity_effects(scene,.2);CHECK_FALSE(scene.follow_trail_entities);CHECK(pixels().rgba8==wall_only.rgba8);
        GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);CHECK(depth==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    }
    gl->release_renderer();
}
TEST_CASE("E22 scene illumination selects one logical strand and bounded neutral defaults",
          "[e22][e22-scene-light][no-stock][game-api]") {
    egf::Module m;const auto profile=m.host.beam_scene_light_profile("sprites/xbeam1.spr",true);
    CHECK(profile.radius_units==192);CHECK(profile.intensity==Catch::Approx(.65*.7));CHECK(profile.spacing_units==192);
    REQUIRE(profile.color_override);CHECK((*profile.color_override==std::array<float,3>{.5F,.5F,1.F}));
    CHECK_FALSE(profile.inherit_beam_opacity);
    const egf::api::BeamSceneLightProfile defaults;
    CHECK_FALSE(defaults.color_override);CHECK(defaults.inherit_beam_opacity);
    CHECK(egf::api::valid_beam_scene_light(defaults));
    CHECK(m.host.beam_scene_light_profile("sprites/xbeam1.spr",false).intensity==0);
    for(const auto name:{"sprites/laserbeam.spr","sprites/XSpark1.spr","sprites/smoke.spr",""})
        CHECK(m.host.beam_scene_light_profile(name,true).intensity==0);
    for(const auto invalid:std::array<egf::api::BeamSceneLightProfile,8>{{
        {-1,.5F,192},{513,.5F,192},{192,1.01F,192},{192,-1,192},
        {192,.5F,0},{0,.5F,192},{192,std::numeric_limits<float>::quiet_NaN(),192},
        {192,.5F,std::numeric_limits<float>::infinity()}}})CHECK_FALSE(egf::api::valid_beam_scene_light(invalid));
    for(const auto color:std::array<std::array<float,3>,4>{{
        {-1,.5F,1},{.5F,1.01F,1},{.5F,.5F,std::numeric_limits<float>::quiet_NaN()},
        {std::numeric_limits<float>::infinity(),.5F,1}}}) {
        auto invalid=profile;invalid.color_override=color;CHECK_FALSE(egf::api::valid_beam_scene_light(invalid));
    }
    m.command(1,.02,1);m.confirm();egf::World world;
    const auto beams=m.host.beam_effects(world,.1,hlclient::assets::AssetVector3{0,0,32});REQUIRE(beams.count==2);
    CHECK(beams.sprites[0].scene_light.intensity==Catch::Approx(.65));CHECK(beams.sprites[1].scene_light.intensity==0);
    CHECK_FALSE(beams.sprites[0].scene_light.color_override);CHECK(beams.sprites[0].scene_light.inherit_beam_opacity);
    // The accepted two ribbons and independent halo are not retuned.
    CHECK(beams.sprites[0].width==3.5F);CHECK(beams.sprites[1].width==5);
    CHECK(beams.sprites[0].ribbon_glow.width_scale==4);CHECK(beams.sprites[0].ribbon_glow.opacity==Catch::Approx(.35));
    m.host.teardown();CHECK(m.host.beam_scene_light_profile("sprites/xbeam1.spr",true).intensity==0);
}
TEST_CASE("E22 scene light sampling preserves endpoints budget frame identity and cleanup",
          "[e22][e22-scene-light][no-stock][composition]") {
    using V=hlclient::assets::AssetVector3;namespace rr=hlclient::renderer;
    egon_assets::Owned owned;egf::Module m;m.command(1,.02,1);m.confirm();egf::World world;
    auto beams=m.host.beam_effects(world,.1,V{0,0,32});REQUIRE(beams.count==2);
    for(auto& beam:std::span{beams.sprites}.first(beams.count)){beam.start={0,0,32};beam.end={2048,0,32};}
    rr::RenderScene scene;
    const auto compose=[&] {scene={};owned.assets->present_entity_effects(scene,.1,{beams.sprites.data(),beams.count});REQUIRE(scene.follow_trail_entities);};
    compose();CHECK(scene.world_effect_light_count==0);owned.assets->append_beam_lights(scene);REQUIRE(scene.world_effect_light_count==4);
    const auto original=scene.world_effect_lights;
    CHECK(original[0].center.x==48);CHECK(original[3].center.x==2000);
    for(const auto& light:scene.effect_lights()){CHECK(rr::valid_render_point_light(light));CHECK(light.intensity==Catch::Approx(.65*.7));}
    owned.assets->append_beam_lights(scene);CHECK(scene.world_effect_light_count==4); // consuming, not an effect queue
    for(auto& beam:std::span{beams.sprites}.first(beams.count))std::swap(beam.start,beam.end);
    compose();owned.assets->append_beam_lights(scene);REQUIRE(scene.world_effect_light_count==4);
    for(unsigned i=0;i<4;++i){CHECK(scene.world_effect_lights[i].center.x==Catch::Approx(original[i].center.x));CHECK(scene.world_effect_lights[i].intensity==original[i].intensity);}
    compose();scene.world_effect_light_count=7;
    for(unsigned i=0;i<7;++i)scene.world_effect_lights[i]={{float(i),9,8},64,.25F,{1,0,0}};
    owned.assets->append_beam_lights(scene);REQUIRE(scene.world_effect_light_count==8);
    for(unsigned i=0;i<7;++i){CHECK(scene.world_effect_lights[i].center.x==float(i));CHECK(scene.world_effect_lights[i].intensity==.25F);}
    CHECK(scene.world_effect_lights[7].center.x==48); // nearest survives a one-slot remainder
    compose();scene.world_effect_light_count=8;const auto full=scene.world_effect_lights;owned.assets->append_beam_lights(scene);
    CHECK(scene.world_effect_light_count==8);for(unsigned i=0;i<8;++i)CHECK(scene.world_effect_lights[i].intensity==full[i].intensity);
    compose();rr::RenderScene unrelated;owned.assets->append_beam_lights(unrelated);CHECK(unrelated.world_effect_light_count==0);
    owned.assets->append_beam_lights(scene);CHECK(scene.world_effect_light_count==0);
    // No stale light survives an empty/invalid frame, including rejected final frame publication.
    compose();owned.assets->present_entity_effects(scene,.2);owned.assets->append_beam_lights(scene);CHECK(scene.world_effect_light_count==0);
    compose();owned.assets->present_entity_effects(scene,std::numeric_limits<double>::quiet_NaN());owned.assets->append_beam_lights(scene);CHECK(scene.world_effect_light_count==0);
    beams.sprites[1].noise_amplitude=std::numeric_limits<float>::quiet_NaN();
    scene={};owned.assets->present_entity_effects(scene,.1,{beams.sprites.data(),beams.count});CHECK_FALSE(scene.follow_trail_entities);
    owned.assets->append_beam_lights(scene);CHECK(scene.world_effect_light_count==0);beams.sprites[1].noise_amplitude=.8F;
    for(unsigned reason=0;reason<5;++reason) {
        CAPTURE(reason);auto bad=beams.sprites[0];scene={};
        if(reason==0)bad.scene_light.radius_units=std::numeric_limits<float>::quiet_NaN();
        if(reason==1)bad.scene_light.intensity=2;
        if(reason==2)bad.action.generation=2;
        if(reason==3)bad.virtual_sprite.fill('\0');
        if(reason==4)bad.expires=.09;
        owned.assets->present_entity_effects(scene,.1,{&bad,1});owned.assets->append_beam_lights(scene);CHECK(scene.world_effect_light_count==0);
    }
    std::array<egf::api::BeamSpriteFrame,6> emitters;
    for(unsigned i=0;i<emitters.size();++i){emitters[i]=beams.sprites[0];emitters[i].action.emitter_entity=i+1;
        emitters[i].start={float(i)*400,0,32};emitters[i].end={float(i)*400+96,0,32};}
    scene={};owned.assets->present_entity_effects(scene,.1,emitters);owned.assets->append_beam_lights(scene);
    REQUIRE(scene.world_effect_light_count==4);for(const auto& light:scene.effect_lights())CHECK(light.center.x<=1248);
    emitters[1].start=emitters[0].start;emitters[1].end=emitters[0].end;
    scene={};owned.assets->present_entity_effects(scene,.1,std::span{emitters}.first(2));owned.assets->append_beam_lights(scene);
    CHECK(scene.world_effect_light_count==1); // duplicate identical submitted geometry cannot double its power
    compose();auto reset=egf::state();reset.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,
        egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{1,1,0,0,8}};
    reset.client_metadata=reset.server_time_metadata=reset.entity_metadata;reset.server_time_seconds=0;
    reset.canonical_state_hash=egf::c::runtime_observation_canonical_hash(reset);REQUIRE(owned.assets->reset_generation(reset,owned.world));
    owned.assets->append_beam_lights(scene);CHECK(scene.world_effect_light_count==0);
}
TEST_CASE("E22 local and observer illumination lights world and Studio pixels without beam pixels",
          "[e22][e22-scene-light][no-stock][opengl][composition]") {
    using V=hlclient::assets::AssetVector3;namespace rr=hlclient::renderer;namespace er=hlclient::entity_render;
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);
    if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");
    gl->initialize_renderer();
    std::optional<rr::RenderPointLight> dim_attached_light;
    std::vector<std::uint8_t> dim_attached_world,dim_attached_model;
    for(const unsigned mode:{0U,1U,2U,3U}) {
        const bool server=mode!=0,attached=mode>=2,dim=mode==2;
        CAPTURE(mode);egon_assets::Owned owned(true);egf::Module m;m.command(1,.02,1);m.confirm();
        rr::RenderScene scene;scene.camera={{96,0,32},{0,0,32},{0,0,1},1.04719755F,.1F,8192};scene.clear_color={0,0,0,1};
        auto state=egf::state();state.packet_entities.clear();
        const auto publish=[&] {
            ++state.publication_revision;const auto rev=state.publication_revision;
            state.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,
                egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{rev,static_cast<std::size_t>(rev),0,0,8}};
            state.client_metadata=state.server_time_metadata=state.entity_metadata;state.server_time_seconds=.1;
            state.canonical_state_hash=egf::c::runtime_observation_canonical_hash(state);REQUIRE(egf::c::valid_runtime_observation(state));
            REQUIRE(owned.assets->project_entities(state,owned.world));REQUIRE(owned.assets->present_entities(m.host,.1,1,owned.world,{320,240}));
            if(attached)REQUIRE(owned.assets->present_entities(m.host,.3,1,owned.world,{320,240}));
        };
        if(server) {
            if(attached) {
                egf::c::RuntimePacketEntityObservation p;p.entity_number=2;
                p.ordinary_visual_schema=p.player_movement_schema=true;p.model_index=7;
                p.origin={0.,-48.,32.};p.angles={0.,0.,0.};p.sequence=0;p.frame=0;p.body=0;p.skin=0;
                p.animation_time_seconds=.1;p.frame_rate=1;p.gait_sequence=0;
                p.controllers.fill(127);p.blending.fill(127);p.weapon_model_index=71;state.packet_entities.push_back(p);
            }
            for(unsigned i=0;i<2;++i) {
                egf::c::RuntimePacketEntityObservation e;e.entity_number=3+i;e.origin={0.,48.,32.};e.angles={0.,-48.,32.};
                e.custom_beam.emplace();auto& b=*e.custom_beam;
                b.type=attached ? egf::c::RuntimeCustomBeamObservation::Type::entity_point : egf::c::RuntimeCustomBeamObservation::Type::points;
                if(attached)b.end_reference={2,1};
                b.flags=static_cast<std::uint8_t>(i ? 0 : 16);b.sprite_model_index=75;b.width_units=i ? 5 : 3.5;
                b.noise_raw=i ? 8 : 20;b.scroll_rate=i ? 2.5 : 5.5;b.color={128,128,255};b.opacity=178;
                if(attached) {
                    // Pinned SDK CEgon::CreateEffect/UpdateEffect wide-mode
                    // ribbon values, after the custom delta .1 width scaling.
                    b.width_units=i ? 5.5 : (dim ? 2 : 4);b.scroll_rate=i ? 2.5 : 5;
                    b.color=i ? std::array<std::uint8_t,3>{50,50,255} :
                        dim ? std::array<std::uint8_t,3>{55,60,64} : std::array<std::uint8_t,3>{30,30,144};
                    b.opacity=static_cast<std::uint8_t>(i ? 100 : dim ? 75 : 255);
                }
                b.complete=b.supported=true;state.packet_entities.push_back(e);
            }
            publish();owned.assets->present_entity_effects(scene,.1);
            if(attached) {
                REQUIRE(owned.assets->remote_weapon_poses().size()==1);
                const auto& pose=owned.assets->remote_weapon_poses()[0];
                CHECK(pose.entity==2);CHECK(pose.semantic_key==10);
                CHECK(pose.source==egf::api::RemoteSocketSource::validated_pmodel_socket);
                REQUIRE(scene.follow_trail_entities);const auto instances=scene.follow_trail_entities->frame->sprite_instances();
                REQUIRE(instances.size()==2);const auto& main=instances[0];REQUIRE(main.ribbon_end);
                CHECK(main.ribbon_end->x==pose.muzzle.origin.x);CHECK(main.ribbon_end->y==pose.muzzle.origin.y);
                CHECK(main.ribbon_end->z==pose.muzzle.origin.z);CHECK(main.ribbon_sine_noise);
                CHECK(main.tint[0]==Catch::Approx((dim ? 55 : 30)/255.));
                CHECK(main.tint[1]==Catch::Approx((dim ? 60 : 30)/255.));
                CHECK(main.tint[2]==Catch::Approx((dim ? 64 : 144)/255.));
                CHECK(main.opacity==Catch::Approx((dim ? 75 : 255)/255.));
                CHECK(main.ribbon_width==Catch::Approx(dim ? 2 : 4));
                CHECK_FALSE(instances[1].ribbon_sine_noise);CHECK(instances[1].opacity==Catch::Approx(100/255.));
            }
        } else {
            struct Wall:egf::api::IStaticEffectTrace {egf::api::StaticEffectTrace trace(V,V)const noexcept override{return {true,false,false,true,false,{0,48,32},{0,-1,0}};}} wall;
            (void)m.host.local_visuals({{0,-48,32},{0,1,0},{1,0,0},{0,0,1},{},.1});
            const auto beams=m.host.beam_effects(wall,.1,V{0,-48,32});REQUIRE(beams.count==2);
            owned.assets->present_entity_effects(scene,.1,{beams.sprites.data(),beams.count});
        }
        REQUIRE(scene.follow_trail_entities);const auto ribbon_frame=scene.follow_trail_entities->frame;
        owned.assets->append_beam_lights(scene);REQUIRE(scene.world_effect_light_count==1); // two strands, one light
        CHECK(scene.follow_trail_entities->frame==ribbon_frame);
        if(!attached)CHECK(scene.world_effect_lights[0].center.y==Catch::Approx(0));
        CHECK(scene.world_effect_lights[0].intensity==Catch::Approx(.65*.7));
        CHECK((scene.world_effect_lights[0].color==std::array<float,3>{.5F,.5F,1.F}));
        if(dim)dim_attached_light=scene.world_effect_lights[0];
        if(mode==3) {
            REQUIRE(dim_attached_light);const auto& light=scene.world_effect_lights[0];
            CHECK(light.center.x==dim_attached_light->center.x);CHECK(light.center.y==dim_attached_light->center.y);
            CHECK(light.center.z==dim_attached_light->center.z);CHECK(light.radius_units==dim_attached_light->radius_units);
            CHECK(light.intensity==dim_attached_light->intensity);CHECK(light.color==dim_attached_light->color);
        }
        scene.follow_trail_entities.reset(); // isolate actual lit objects, not additive beam/halo coverage
        const auto lit_scene=scene;
        const auto pixels=[&] {gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
            auto result=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(result);return result;};
        namespace wf=hlclient::tests::world_render_fixture;
        auto wall=wf::make_world();for(auto& vertex:wall.vertices){vertex.position={-16,-64+vertex.position.x*8,-32+vertex.position.y*8};vertex.normal={1,0,0};}
        wall.bounds={{-16,-64,-32},{-16,64,96}};wall.surfaces[0].bounds=wall.bounds;
        auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build({std::move(wall),wf::make_texture_set(false)},wf::make_lightmap_set({}));REQUIRE(built);
        scene.static_world=rr::RenderStaticWorld{std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package))};
        scene.world_effect_light_count=0;const auto dark_wall=pixels();scene.world_effect_light_count=1;const auto light_wall=pixels();
        unsigned wall_changed=0;for(std::size_t i=0;i<light_wall.rgba8.size();i+=4)if(light_wall.rgba8[i+2]>dark_wall.rgba8[i+2]+5)++wall_changed;
        CHECK(wall_changed>100);scene.world_effect_lights[0].center.x=512;CHECK(pixels().rgba8==dark_wall.rgba8);
        scene=lit_scene;
        egf::api::ViewmodelIntent intent;intent.status=egf::api::ViewmodelStatus::ready;intent.generation=1;intent.publication_revision=1;intent.model_index=73;
        auto model=owned.assets->materialize_viewmodel(intent);REQUIRE(model);const auto& old=*model->frame;
        er::EntityRenderFrameBuildInput input;input.resource_id=old.resource_id();input.resource_revision=old.resource_revision()+1;input.interpolation=old.interpolation();
        input.studio_poses.assign(old.studio_poses().begin(),old.studio_poses().end());input.studio_instances.assign(old.studio_instances().begin(),old.studio_instances().end());
        for(auto& instance:input.studio_instances){instance.transform.origin={0,0,32};instance.transform.uniform_scale=4;instance.static_light_rgb=std::array{.15F,.15F,.15F};instance.interpolated_bounds={{0,-32,0},{80,32,64}};}
        auto frame=er::EntityRenderFrameBuilder{}.build(*model->package,std::move(input));REQUIRE(frame);model->frame=std::make_shared<const er::EntityRenderFrame>(std::move(*frame.frame));
        scene.dynamic_entities=*model;scene.world_effect_light_count=0;const auto dark_model=pixels();REQUIRE(dark_model.non_clear_pixel_count>10);
        scene.world_effect_light_count=1;const auto light_model=pixels();unsigned model_changed=0;
        for(std::size_t i=0;i<light_model.rgba8.size();i+=4)if(light_model.rgba8[i]>dark_model.rgba8[i]+3 || light_model.rgba8[i+2]>dark_model.rgba8[i+2]+3)++model_changed;
        CHECK(model_changed>20);scene.world_effect_lights[0].center.x=512;CHECK(pixels().rgba8==dark_model.rgba8);
        if(dim){dim_attached_world=light_wall.rgba8;dim_attached_model=light_model.rgba8;}
        if(mode==3){CHECK(light_wall.rgba8==dim_attached_world);CHECK(light_model.rgba8==dim_attached_model);}
        std::cout<<"egon-scene-light server="<<server<<" attached="<<attached<<" dim="<<dim
            <<" world_pixels="<<wall_changed<<" studio_pixels="<<model_changed<<"\n";
        egon_pixels::save(attached ? (dim ? "observer-attached-dim-lit-world.ppm" : "observer-attached-bright-lit-world.ppm") :
            server ? "observer-lit-world.ppm" : "local-lit-world.ppm",light_wall);
        egon_pixels::save(attached ? (dim ? "observer-attached-dim-lit-studio.ppm" : "observer-attached-bright-lit-studio.ppm") :
            server ? "observer-lit-studio.ppm" : "local-lit-studio.ppm",light_model);
        if(attached) {
            // The independent light policy does not turn an invisible main
            // strand, or the second non-sine ribbon alone, into an emitter.
            state.packet_entities[1].custom_beam->opacity=0;publish();scene.world_effect_light_count=0;
            owned.assets->present_entity_effects(scene,.1);owned.assets->append_beam_lights(scene);
            CHECK(scene.world_effect_light_count==0);REQUIRE(scene.follow_trail_entities);
            CHECK(scene.follow_trail_entities->frame->sprite_instances()[0].opacity==0);
            scene.follow_trail_entities.reset();CHECK(pixels().rgba8==dark_model.rgba8);
            state.packet_entities.erase(state.packet_entities.begin()+1);publish();scene.world_effect_light_count=0;
            owned.assets->present_entity_effects(scene,.1);owned.assets->append_beam_lights(scene);
            CHECK(scene.world_effect_light_count==0);REQUIRE(scene.follow_trail_entities);
            CHECK(scene.follow_trail_entities->frame->sprite_instances().size()==1);
            scene.follow_trail_entities.reset();CHECK(pixels().rgba8==dark_model.rgba8);
        }
        if(server){state.packet_entities.clear();publish();}else m.command(2,.2,0);
        scene.world_effect_light_count=0;owned.assets->present_entity_effects(scene,.2);owned.assets->append_beam_lights(scene);
        CHECK(scene.world_effect_light_count==0);CHECK_FALSE(scene.follow_trail_entities);CHECK(pixels().rgba8==dark_model.rgba8);
    }
    gl->release_renderer();
}
TEST_CASE("E22 repeated fire variants preserve actual Studio viewmodel muzzle and beam",
          "[e22][e22-single-frame][no-stock][composition][opengl]") {
    egon_assets::Owned owned;egf::Module m;const auto model=owned.assets->presentation_model(1,73);REQUIRE(model);m.host.bind_model(model);m.host.observe(m.s,0);
    egf::World w;unsigned command=0;const std::array sequences{1U,4U,7U,10U,13U,16U};
    for(unsigned episode=0;episode<sequences.size();++episode) {
        const auto start=.02+episode*1.;const auto sequence=sequences[episode];m.command(sequence,start,1);command=sequence;m.confirm(start+.01);
        for(const double age:{.001,.017,.049,.125}) {
            const auto at=start+age;const auto presentation=m.host.sample(at);REQUIRE(presentation.visual);
            const auto intent=m.host.viewmodel(m.s,model,at,presentation.visual);REQUIRE(intent.status==egf::api::ViewmodelStatus::ready);
            const auto view=owned.assets->materialize_viewmodel(intent);INFO(hlclient::app::to_string(owned.assets->first_person_status()));REQUIRE(view);REQUIRE(owned.assets->viewmodel_attachment_zero());
            (void)m.host.local_visuals({{0,0,32},{0,1,0},{1,0,0},{0,0,1},{},at});const auto beam=m.host.beam_effects(w,at,hlclient::assets::AssetVector3{0,0,30});CHECK(beam.count==2);
        }
        m.command(command+1,start+.2,0);m.s.publication_revision++;m.s.weapon_slots[0].auxiliary_integer3=0;m.host.observe(m.s,start+.21);(void)m.host.drain_audio();
    }
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");gl->initialize_renderer();
    hlclient::renderer::RenderScene scene;scene.camera={{96,-64,48},{0,40,30},{0,0,1},1.04719755F,.1F,8192};gl->renderer().render(scene,{320,240});const auto blank=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    for(const auto seq:{5U,6U,7U,8U}) {
        egf::api::LocalWeaponVisual visual{seq,1,100+seq,10,egf::api::LocalWeaponAnimationSource::provisional_command,egf::api::LocalWeaponActionStatus::predicted_pending};
        const auto intent=m.host.viewmodel(m.s,model,10.125,visual);REQUIRE(intent.status==egf::api::ViewmodelStatus::ready);REQUIRE(intent.sequence==seq);
        scene.first_person_entities=owned.assets->materialize_viewmodel(intent);REQUIRE(scene.first_person_entities);REQUIRE(owned.assets->viewmodel_attachment_zero());
        gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);const auto image=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);CHECK_FALSE(std::ranges::equal(blank.rgba8,image.rgba8));
    }
    gl->release_renderer();
}
TEST_CASE("E22 focus mute resumes current run only and stop while muted cannot resurrect it","[e22][no-stock][audio][focus]"){
    egon_assets::Owned owned;hlclient::goldsrc::ApprovedSoundAssets loader(owned.assets->sound_resources());egon_assets::ready(loader);egf::Module m;
    hlclient::audio::OfflineOutput output;hlclient::goldsrc::LocalAudio delivery(output);
    m.command(1,.02,1);m.confirm();(void)m.host.drain_audio();(void)m.host.sample(2.03);
    auto batch=m.host.drain_audio();delivery.update(batch,&loader,2.03,false);CHECK(output.mixer.active()==0);
    batch.count=batch.control_count=batch.prepare_count=0;
    for(unsigned i=0;i<125 && output.mixer.active()==0;++i){delivery.update(batch,&loader,4+i*.001,true);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    REQUIRE(output.mixer.active()==1);CHECK(output.mixer.statistics().started==1); // no replayed startup
    delivery.update(batch,&loader,4.2,false);CHECK(output.mixer.active()==0);
    m.command(2,4.3,0);delivery.update(m.host.drain_audio(),&loader,4.3,false);delivery.update(batch,&loader,5,true);CHECK(output.mixer.active()==0);
}
TEST_CASE("E22 server main noise current merged socket update removal and local handoff","[e22][no-stock][composition][custom-beam]"){
    egon_assets::Owned owned;egf::Module m;auto s=egf::state();s.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{10,10,0,0,8}};
    s.client_metadata=s.server_time_metadata=s.entity_metadata;s.server_time_seconds=1;s.publication_revision=10;
    egf::c::RuntimePacketEntityObservation p;p.entity_number=2;p.ordinary_visual_schema=p.player_movement_schema=true;p.model_index=7;p.origin={0.,0.,16.};p.angles={0.,0.,0.};p.sequence=0;p.frame=0;p.body=0;p.skin=0;
    p.animation_time_seconds=1;p.frame_rate=1;p.gait_sequence=0;p.controllers.fill(127);p.blending.fill(127);p.weapon_model_index=71;s.packet_entities.push_back(p);
    for(unsigned i=0;i<2;++i){egf::c::RuntimePacketEntityObservation e;e.entity_number=3+i;e.origin={0.,80.,30.};e.custom_beam.emplace();auto& b=*e.custom_beam;
        b.type=egf::c::RuntimeCustomBeamObservation::Type::entity_point;b.end_reference={2,1};b.flags=static_cast<std::uint8_t>(i ? 0 : 16);b.sprite_model_index=75;b.width_units=i ? 5.5 : 4;
        b.noise_raw=i ? 8 : 20;b.scroll_rate=i ? 2.5 : 5;b.color={50,50,125};b.opacity=255;b.complete=b.supported=true;s.packet_entities.push_back(e);}
    const auto publish=[&](double now){s.canonical_state_hash=egf::c::runtime_observation_canonical_hash(s);REQUIRE(owned.assets->project_entities(s,owned.world));
        REQUIRE(owned.assets->present_entities(m.host,now,1,owned.world,{320,240}));REQUIRE(owned.assets->present_entities(m.host,now+.2,1,owned.world,{320,240}));};
    publish(10);REQUIRE(owned.assets->remote_weapon_poses().size()==1);CHECK(owned.assets->remote_weapon_poses()[0].semantic_key==10);CHECK(owned.assets->remote_weapon_poses()[0].muzzle_bone==9);
    hlclient::renderer::RenderScene scene;owned.assets->present_entity_effects(scene,10.2);REQUIRE(scene.follow_trail_entities);const auto initial=scene.follow_trail_entities->frame->sprite_instances();REQUIRE(initial.size()==2);
    CHECK(initial[0].entity_number==3);CHECK(initial[1].entity_number==4);REQUIRE(initial[0].ribbon_end);const auto endpoint=*initial[0].ribbon_end;
    CHECK(endpoint.x==owned.assets->remote_weapon_poses()[0].muzzle.origin.x);CHECK(endpoint.z!=16);CHECK(initial[0].transform.origin.y==80);
    s.publication_revision=11;s.server_time_seconds=1.1;s.entity_metadata.source->record_identity=s.entity_metadata.source->record_ordinal=11;s.packet_entities[0].origin.x=30.;publish(11);
    owned.assets->present_entity_effects(scene,11.2);REQUIRE(scene.follow_trail_entities);CHECK(scene.follow_trail_entities->frame->sprite_instances()[0].ribbon_end->x==Catch::Approx(endpoint.x+30));
    s.packet_entities.resize(1);s.publication_revision=12;s.entity_metadata.source->record_identity=s.entity_metadata.source->record_ordinal=12;s.server_time_seconds=1.2;publish(12);owned.assets->present_entity_effects(scene,12.2);CHECK_FALSE(scene.follow_trail_entities);
    m.command(1,.02,1);m.confirm();egf::World w;CHECK(m.host.beam_effects(w,.1,hlclient::assets::AssetVector3{}).count==2);
    std::array<egf::c::RuntimePacketEntityObservation,2> local{};for(unsigned i=0;i<2;++i){auto& e=local[i];e.entity_number=13+i;e.custom_beam.emplace();auto& b=*e.custom_beam;b.type=egf::c::RuntimeCustomBeamObservation::Type::entity_point;b.end_reference={1,1};b.sprite_model_index=75;b.width_units=4;b.flags=static_cast<std::uint8_t>(i ? 0 : 16);b.complete=b.supported=true;}
    m.host.remote_entities(local,8,13);CHECK(m.host.beam_effects(w,.2,hlclient::assets::AssetVector3{}).count==0);
}
TEST_CASE("E22 approved read-only Egon metadata control","[e22][optional-assets]"){
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count=0;if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;std::free(value);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())SKIP("Exact approved read-only root opt-in absent");
    namespace t=hlclient::tests;namespace app=hlclient::app;
    auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0},{2,"models/v_egon.mdl",73,0,0},{2,"models/p_egon.mdl",71,0,0},{2,"models/w_egon.mdl",74,0,0},{2,"sprites/xbeam1.spr",75,0,0},{2,"sprites/XSpark1.spr",76,0,0}});
    auto made=app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/crossfire.bsp"),root,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(made.projection);
    egf::c::ClientWorldState world;auto initial=egf::state();initial.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{1,1,0,0,8}};
    initial.server_time_metadata=initial.client_metadata=initial.entity_metadata;initial.server_time_seconds=0;initial.canonical_state_hash=egf::c::runtime_observation_canonical_hash(initial);
    const auto reset=made.projection->reset_generation(initial,world);INFO((reset.error ? reset.error->context : "ready"));REQUIRE(reset);
    auto meta=made.projection->presentation_model(1,73);REQUIRE(meta);
    for(unsigned i=0;i<meta->sequences.size();++i)std::cout<<"egon-sequence "<<i<<" "<<meta->sequences[i].label<<" "<<meta->sequences[i].frame_count<<" "<<meta->sequences[i].fps<<" loop="<<meta->sequences[i].looping<<"\n";
    for(unsigned i=0;i<2;++i)if(meta->supported_bodies[i])std::cout<<"egon-body "<<i<<"\n";
    for(const auto slot:{73,71}){auto model=made.projection->studio_model_data(slot);REQUIRE(model);std::cout<<"egon-model "<<slot<<" bones="<<model->bones.size()<<" attachments="<<model->attachments.size()<<" skins="<<model->skin_families.size()<<"\n";
        for(unsigned i=0;i<model->bones.size();++i) {const auto& b=model->bones[i];std::cout<<"egon-bone "<<slot<<" "<<i<<" "<<b.name<<" T="<<b.default_translation.x<<","<<b.default_translation.y<<","<<b.default_translation.z<<" R="<<b.default_rotation_radians.x<<","<<b.default_rotation_radians.y<<","<<b.default_rotation_radians.z<<"\n";}
        for(const auto& a:model->attachments)std::cout<<"egon-attachment "<<slot<<" "<<a.bone_index<<" "<<a.local_origin.x<<","<<a.local_origin.y<<","<<a.local_origin.z<<"\n";
        hlclient::assets::AssetVector3 lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};for(const auto& sub:model->submodels)for(const auto& v:sub.vertices){auto p=v.source_position;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
        std::cout<<"egon-bounds "<<slot<<" "<<lo.x<<","<<lo.y<<","<<lo.z<<" "<<hi.x<<","<<hi.y<<","<<hi.z<<"\n";}
    const auto p=made.projection->studio_model_data(71);hlclient::goldsrc::studio::StudioPoseInput pose_input;
    const auto pose=hlclient::goldsrc::studio::StudioPoseEvaluator{}.evaluate({"egon-audit",{}},*p,pose_input);REQUIRE(pose);
    float front=-1e9F;hlclient::assets::AssetVector3 local{};unsigned bone=0;
    for(const auto& sub:p->submodels)for(const auto& v:sub.vertices){const auto& b=pose.pose->world_bones()[v.position_bone_index].transform.values;const auto point=v.source_position;const auto x=b[0]*point.x+b[1]*point.y+b[2]*point.z+b[3];if(x>front){front=x;local=point;bone=v.position_bone_index;}}
    std::cout<<"egon-p-reference-front "<<bone<<" "<<front<<" local="<<local.x<<","<<local.y<<","<<local.z<<"\n";
    egf::Module module;const auto socket=module.host.remote_weapon_socket_plan({71,{},"models/p_egon.mdl",*p});
    CHECK(socket.source==egf::api::RemoteSocketSource::validated_pmodel_socket);CHECK(socket.semantic_key==10);CHECK(socket.muzzle.bone_index==9);
    CHECK(hlclient::games::halflife::egon_sequences(*meta));
    egf::Module fire_module;fire_module.host.bind_model(meta);fire_module.host.observe(fire_module.s,0);
    for(const auto sequence:{5U,6U,7U,8U})for(const double age:{.001,.017,.125,.999}) {
        CAPTURE(sequence,age);egf::api::LocalWeaponVisual visual{sequence,1,100+sequence,.02,egf::api::LocalWeaponAnimationSource::provisional_command,egf::api::LocalWeaponActionStatus::predicted_pending};
        const auto intent=fire_module.host.viewmodel(fire_module.s,meta,.02+age,visual);REQUIRE(intent.status==egf::api::ViewmodelStatus::ready);
        const auto view=made.projection->materialize_viewmodel(intent);INFO(hlclient::app::to_string(made.projection->first_person_status()));REQUIRE(view);REQUIRE(made.projection->viewmodel_attachment_zero());
    }
    hlclient::goldsrc::ApprovedSoundAssets loader(made.projection->sound_resources());egon_assets::ready(loader);
    for(const auto& sound:hlclient::games::halflife::EgonPresentation::resources()){const auto a=loader.request_local(sound);REQUIRE(a.asset);std::cout<<"egon-audio "<<sound.name()<<" rate="<<a.asset->sample_rate<<" channels="<<a.asset->channel_count<<" samples="<<a.asset->interleaved_samples.size()<<"\n";}
    hlclient::game_api::BeamSpriteFrame beam;beam.action.generation=1;beam.starts=.1;beam.expires=.2;beam.start={0,0,32};beam.end={0,80,32};beam.width=4;
    constexpr std::string_view name="sprites/xbeam1.spr";std::copy(name.begin(),name.end(),beam.virtual_sprite.begin());hlclient::renderer::RenderScene scene;
    made.projection->present_entity_effects(scene,.1,{&beam,1});REQUIRE(scene.follow_trail_entities);CHECK(scene.follow_trail_entities->frame->sprite_instances().size()==1);
    const auto& instance=scene.follow_trail_entities->frame->sprite_instances().front();
    const auto& image=scene.follow_trail_entities->package->sprite_assets()[instance.sprite_asset_index]->frames()[instance.selected_frame_index];
    const auto width=image.geometry.width,height=image.geometry.height;REQUIRE(width>=4);REQUIRE(height>=2);
    const auto column=[&](unsigned x) {unsigned sum=0;for(unsigned y=0;y<height;++y)sum+=std::to_integer<unsigned>(image.rgba8[(y*width+x)*4+2]);return sum/height;};
    std::cout<<"egon-xbeam-profile width="<<width<<" height="<<height<<" blue-columns=";
    for(const auto x:{0U,width/4,width/2,width*3/4,width-1})std::cout<<column(x)<<",";std::cout<<"\n";
    CHECK(column(width/2)>column(0));CHECK(column(width/2)>column(width-1));
    auto gl=t::entity_opengl_fixture::try_context(320,240);
    if(!gl || !t::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable for approved sprite control");
    gl->initialize_renderer();scene.camera={{96,48,32},{0,48,32},{0,0,1},1.04719755F,.1F,8192};scene.clear_color={0,0,0,1};
    beam.end={0,96,32};beam.width=8;beam.color={1,1,1};beam.opacity=1;beam.persistent=true;
    made.projection->present_entity_effects(scene,.1,{&beam,1});gl->renderer().render(scene,{320,240});
    const auto profile=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(profile);
    egon_pixels::save("approved-xbeam-profile.ppm",profile);
    for(const unsigned x:{80U,160U,240U}) {
        unsigned bright=0;for(unsigned y=0;y<240;++y)if(profile.rgba8[(y*320+x)*4+2]>64)++bright;
        std::cout<<"egon-xbeam-gpu x="<<x<<" bright_width="<<bright<<"\n";
        CAPTURE(x,bright);CHECK(bright>=3);CHECK(bright<=8);
    }
    std::array pair{beam,beam};pair[0].end=pair[1].end={0,96,32};
    for(unsigned i=0;i<2;++i) {auto& b=pair[i];b.action.sub_identity=i+1;b.persistent=true;
        b.width=i ? 5.F : 3.5F;b.noise_amplitude=i ? .8F : 2.F;b.sine_noise=!i;b.phase_seconds=.1;
        b.color={.5F,.5F,1};b.opacity=.7F;}
    const auto capture=[&] {made.projection->present_entity_effects(scene,.1,pair);gl->renderer().render(scene,{320,240});
        auto pixels=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(pixels);return pixels;};
    const auto twist=capture();CHECK(twist.non_clear_pixel_count>100);egon_pixels::save("approved-xbeam-twist.ppm",twist);
    pair[0].noise_amplitude=0;const auto straight=capture();
    // Test both sides of the wrap, not its old opaque-ribbon pixel area.
    // Transverse width is independently bounded above in the straight control.
    std::size_t upper_wrap_pixels=0,lower_wrap_pixels=0;
    for(std::size_t y=0;y<240;++y)for(std::size_t x=0;x<320;++x)if(y<108 || y>=132) {
        const auto offset=(y*320+x)*4+2;if(twist.rgba8[offset]>straight.rgba8[offset]+8) {
            if(y<108)++lower_wrap_pixels;else ++upper_wrap_pixels;
        }
    }
    CHECK(lower_wrap_pixels>0);CHECK(upper_wrap_pixels>0);CHECK(glGetError()==GL_NO_ERROR);gl->release_renderer();
}
TEST_CASE("E22 endpoint flare obeys committed frame scale NODRAW and removal without predicted duplicate","[e22][no-stock][opengl][endpoint]"){
    egon_assets::Owned owned;auto s=egf::state();s.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,egf::c::RuntimeObservationCompleteness::complete_reconstruction,egf::c::RuntimeObservationSource{1,1,0,0,8}};
    s.client_metadata=s.server_time_metadata=s.entity_metadata;s.server_time_seconds=1;s.packet_entities.clear();egf::c::RuntimePacketEntityObservation flare;
    flare.entity_number=5;flare.ordinary_visual_schema=true;flare.origin={0.,30.,30.};flare.angles={0.,0.,0.};flare.model_index=76;flare.sequence=0;flare.frame=1;flare.visual_scale=1.5;
    flare.body=0;flare.skin=0;flare.render_mode=3;flare.render_effect=14;flare.render_amount=200;flare.effects=0;s.packet_entities.push_back(flare);
    const auto publish=[&](){s.canonical_state_hash=egf::c::runtime_observation_canonical_hash(s);REQUIRE(owned.assets->project_entities(s,owned.world));};publish();
    REQUIRE(owned.world.entity_frame()->sprite_instances().size()==1);const auto instance=owned.world.entity_frame()->sprite_instances()[0];CHECK(instance.selected_frame_index==1);CHECK(instance.transform.uniform_scale==1.5F);CHECK(instance.opacity==Catch::Approx(200./255.));
    auto gl=hlclient::tests::entity_opengl_fixture::try_context(320,240);if(!gl || !hlclient::tests::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");gl->initialize_renderer();
    auto scene=egf::c::build_render_scene(owned.world);scene.static_world.reset();scene.camera={{96,-64,48},{0,30,30},{0,0,1},1.04719755F,.1F,8192};gl->renderer().render(scene,{320,240});CHECK(gl->renderer().observe_framebuffer({320,240},scene.clear_color,true).non_clear_pixel_count>0);
    s.publication_revision=2;s.entity_metadata.source->record_identity=s.entity_metadata.source->record_ordinal=2;s.packet_entities[0].effects=128;publish();CHECK(owned.world.entity_frame()->sprite_instances().empty());
    s.publication_revision=3;s.entity_metadata.source->record_identity=s.entity_metadata.source->record_ordinal=3;s.packet_entities[0].effects=0;publish();CHECK(owned.world.entity_frame()->sprite_instances().size()==1);
    s.publication_revision=4;s.entity_metadata.source->record_identity=s.entity_metadata.source->record_ordinal=4;s.packet_entities.clear();publish();CHECK(owned.world.entity_frame()->sprite_instances().empty());
    scene.dynamic_entities=egf::c::build_render_scene(owned.world).dynamic_entities;gl->renderer().render(scene,{320,240});CHECK(gl->renderer().observe_framebuffer({320,240},scene.clear_color,true).non_clear_pixel_count==0);gl->release_renderer();
}

TEST_CASE("E22 server endpoint flare is occluded by world depth through normal materialization",
          "[e22][no-stock][opengl][endpoint][world-glow-occlusion]") {
    namespace r=hlclient::renderer;
    namespace t=hlclient::tests;
    egon_assets::Owned owned;auto state=egf::state();
    state.entity_metadata={1,egf::c::RuntimeObservationFreshness::observed_in_record,
        egf::c::RuntimeObservationCompleteness::complete_reconstruction,
        egf::c::RuntimeObservationSource{1,1,0,0,8}};
    state.client_metadata=state.server_time_metadata=state.entity_metadata;
    state.server_time_seconds=1;
    egf::c::RuntimePacketEntityObservation flare;
    flare.entity_number=5;flare.ordinary_visual_schema=true;flare.origin={8.,12.,-4.};
    flare.angles={0.,0.,0.};flare.model_index=76;flare.sequence=0;flare.frame=1;
    flare.visual_scale=.125;flare.body=0;flare.skin=0;flare.render_mode=3;
    flare.render_effect=14;flare.render_amount=200;flare.effects=0;state.packet_entities={flare};
    const auto publish=[&] {
        state.canonical_state_hash=egf::c::runtime_observation_canonical_hash(state);
        REQUIRE(owned.assets->project_entities(state,owned.world));
    };
    publish();REQUIRE(owned.world.entity_frame()->sprite_instances().size()==1);
    CHECK(owned.world.entity_frame()->sprite_instances()[0].blend_mode==hlclient::entity_render::SpriteBlendMode::glow);
    auto context=t::entity_opengl_fixture::try_context();
    if(!context || !t::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 Core unavailable");
    context->initialize_renderer();
    auto world_result=t::world_render_fixture::make_package();REQUIRE(world_result);
    r::RenderScene scene;scene.camera={{8,-12,6},{8,12,-4},{0,0,1},1.04719755F,.1F,4096};
    scene.static_world=r::RenderStaticWorld{
        std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*world_result.package)),
        r::RenderCullMode::none,r::RenderBaselineLightStylePolicy::source_slot_zero};
    const auto pixels=[&](const r::RenderScene& input) {
        context->renderer().render(input,{96,96});CHECK(glGetError()==GL_NO_ERROR);
        return t::entity_opengl_fixture::framebuffer();
    };
    const auto baseline=pixels(scene);
    scene.dynamic_entities=r::RenderDynamicEntities{owned.world.entity_scene(),owned.world.entity_frame(),{}};
    CHECK(pixels(scene)==baseline);
    auto unobstructed=scene;unobstructed.static_world.reset();
    const auto visible=pixels(unobstructed);unobstructed.dynamic_entities.reset();
    CHECK(visible!=pixels(unobstructed));
    state.packet_entities.front().origin={8.,0.,1.};
    state.publication_revision=2;state.entity_metadata.source->record_identity=state.entity_metadata.source->record_ordinal=2;
    publish();scene.dynamic_entities=r::RenderDynamicEntities{owned.world.entity_scene(),owned.world.entity_frame(),{}};
    CHECK(pixels(scene)!=baseline);
    scene.dynamic_entities.reset();CHECK(pixels(scene)==baseline);
    CHECK(glIsEnabled(GL_DEPTH_TEST)==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    context->release_renderer();
}
