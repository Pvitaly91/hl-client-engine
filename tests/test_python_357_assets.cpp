#include "python_357_test_fixture.hpp"
#include "python_357_pmodel_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/app/live_visual_control.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <cstdlib>
#include <set>
#include <iostream>
namespace f=hlclient::tests::python_fixture;
namespace fixture=hlclient::tests;
namespace api=hlclient::game_api;
namespace c=hlclient::client;
namespace app=hlclient::app;
namespace g=hlclient::goldsrc;
namespace readiness=fixture::readiness_fixture;
namespace r=hlclient::renderer;
using namespace std::chrono_literals;
namespace {
c::RuntimeClientObservationState initial() {
    c::RuntimeClientObservationState s;s.generation=s.publication_revision=1;
    s.server_time_metadata.generation=s.client_metadata.generation=1;
    s.entity_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{1,1,1,0,8}};
    s.client_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{1,1,1,0,8}};
    s.receiving_client=f::state().receiving_client;
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);REQUIRE(c::valid_runtime_observation(s));return s;
}
std::vector<std::byte> owned_python() {
    auto bytes=fixture::synthetic_weapon_presentation_studio();
    const auto seq_offset=fixture::literal_minimal_goldsrc_studio_v10().size();
    fixture::studio_write_i32le(bytes,164,8);
    const auto meta=f::model();
    for(std::size_t i=0;i<8;++i) {
        const auto offset=seq_offset+176*i;
        fixture::studio_write_fixed_string(bytes,offset,32,meta.sequences[i].label);
        fixture::studio_write_f32le(bytes,offset+32,static_cast<float>(meta.sequences[i].fps));
        fixture::studio_write_i32le(bytes,offset+56,static_cast<int>(meta.sequences[i].frame_count));
        fixture::studio_write_i32le(bytes,offset+36,meta.sequences[i].looping ? 1 : 0);
        const auto animation=seq_offset+10*176+i*18;
        bytes[animation+13]=static_cast<std::byte>(meta.sequences[i].frame_count);
    }
    const auto event=bytes.size();bytes.resize(bytes.size()+76);
    fixture::studio_write_i32le(bytes,seq_offset+2*176+48,1);
    fixture::studio_write_i32le(bytes,seq_offset+2*176+52,static_cast<int>(event));
    fixture::studio_write_i32le(bytes,event,0);fixture::studio_write_i32le(bytes,event+4,5001);
    fixture::studio_write_fixed_string(bytes,event+12,64,"31");
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioAttachmentOffset+40,18,0,-1);
    // Owned alternate body geometry, not an embedded stock mesh. Two choices
    // exercise the production body-selection/materialization path on the GPU.
    const auto bodies=bytes.size();bytes.resize(bytes.size()+224);
    for(std::size_t i=0;i<2;++i) std::copy_n(bytes.begin()+fixture::kSyntheticStudioSubmodelOffset,112,bytes.begin()+bodies+112*i);
    // Each body owns disjoint derived tables (the bounded parser rejects
    // overlapping mesh/vertex ranges even when a fixture would share bytes).
    constexpr auto derived_size=fixture::kSyntheticStudioTextureOffset-fixture::kSyntheticStudioMeshOffset;
    for(std::size_t body=0;body<2;++body) {
        const auto derived=bytes.size();bytes.resize(bytes.size()+derived_size);
        std::copy_n(bytes.begin()+fixture::kSyntheticStudioMeshOffset,derived_size,bytes.begin()+derived);
        const auto relocate=[&](std::size_t source){return static_cast<int>(derived+source-fixture::kSyntheticStudioMeshOffset);};
        const auto descriptor=bodies+112*body;
        fixture::studio_write_i32le(bytes,descriptor+76,relocate(fixture::kSyntheticStudioMeshOffset));
        fixture::studio_write_i32le(bytes,descriptor+84,relocate(fixture::kSyntheticStudioVertexBonesOffset));
        fixture::studio_write_i32le(bytes,descriptor+88,relocate(fixture::kSyntheticStudioVerticesOffset));
        fixture::studio_write_i32le(bytes,descriptor+96,relocate(fixture::kSyntheticStudioNormalBonesOffset));
        fixture::studio_write_i32le(bytes,descriptor+100,relocate(fixture::kSyntheticStudioNormalsOffset));
        fixture::studio_write_i32le(bytes,derived+4,relocate(fixture::kSyntheticStudioCommandsOffset));
        if(body) for(std::size_t i=0;i<3;++i)
            fixture::studio_write_vector3(bytes,static_cast<std::size_t>(relocate(fixture::kSyntheticStudioVerticesOffset))+i*12,16,
                i==0 ? -2.0F : i==1 ? 6.0F : 2.0F,i==2 ? 3.0F : -5.0F);
    }
    fixture::studio_write_i32le(bytes,fixture::kSyntheticStudioBodyPartOffset+64,2);
    fixture::studio_write_i32le(bytes,fixture::kSyntheticStudioBodyPartOffset+72,static_cast<int>(bodies));
    fixture::studio_write_i32le(bytes,72,static_cast<int>(bytes.size()));return bytes;
}
std::vector<std::byte> wav() {
    std::vector<std::byte> bytes;
    const auto text=[&](std::string_view s){for(char v:s)bytes.push_back(static_cast<std::byte>(v));};
    const auto number=[&](unsigned n,unsigned width=4){for(unsigned i=0;i<width;++i)bytes.push_back(static_cast<std::byte>((n>>(8*i))&255));};
    text("RIFF");number(36+4800);text("WAVEfmt ");number(16);number(1,2);number(1,2);
    number(22050);number(22050);number(1,2);number(8,2);text("data");number(4800);
    for(unsigned i=0;i<4800;++i)bytes.push_back(static_cast<std::byte>(i%32<16 ? 160 : 96));return bytes;
}
struct Owned {
    fixture::ScopedLocalResourceTestRoot root;
    std::unique_ptr<app::RuntimeReplayLocalAssets> projection;
    Owned() {
        fixture::SyntheticBspBuilder bsp;const std::string entity="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
        const auto entity_bytes=std::as_bytes(std::span{entity});
        bsp.lump(fixture::SyntheticBspLumpId::entities).assign(entity_bytes.begin(),entity_bytes.end());
        fixture::SyntheticWad3Entry texture;texture.name="TEST_QUAD";
        texture.payload=fixture::synthetic_goldsrc_miptex("TEST_QUAD",64,64);
        root.write("valve","test.wad",fixture::synthetic_wad3({texture}).bytes);
        root.write("valve","maps/test.bsp",bsp.build());
        const auto model=owned_python();const auto parsed=g::studio::GoldSrcStudioParser::parse({model,{}, {}});
        INFO((parsed.error ? parsed.error->context : "valid owned model"));REQUIRE(parsed);
        root.write("valve","models/v_357.mdl",model);
        root.write("valve","models/p_357.mdl",f::remote_model(true));
        root.write("valve","models/project_player.mdl",f::remote_model(false));
        for(const auto sample:{"357_shot1","357_shot2","357_reload1","357_cock1"})
            root.write("valve",std::string{"sound/weapons/"}+sample+".wav",wav());
        const auto resources=readiness::parse_resource_list({{2,"maps/test.bsp",9,0,0},{2,"models/v_357.mdl",73,0,0},
            {2,"models/p_357.mdl",71,0,0},{2,"models/project_player.mdl",7,0,0}});
        auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test.bsp"),
            root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
        INFO((created.error ? created.error->context : "ready"));REQUIRE(created.projection);
        projection=std::move(created.projection);c::ClientWorldState world;REQUIRE(projection->reset_generation(initial(),world));
    }
};
struct Sink:hlclient::audio::Output {
    hlclient::audio::OfflineOutput output;unsigned starts{};
    bool submit(const hlclient::audio::Command& cmd) noexcept override {
        if(cmd.operation==hlclient::audio::Operation::start)++starts;return output.submit(cmd);
    }
    void set_listener(hlclient::audio::Listener l) noexcept override {output.set_listener(l);}
};
void pcm(g::ApprovedSoundAssets& loader,const api::LocalAudioBatch& batch,double now) {
    for(std::size_t i=0;i<batch.count;++i) {
        INFO(batch.cues[i].reference.name());
        auto resource=loader.request_local(batch.cues[i].reference);const auto deadline=std::chrono::steady_clock::now()+2s;
        while(resource.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms);resource=loader.request_local(batch.cues[i].reference);
        }
        REQUIRE(resource.status==g::SoundAssetStatus::ready);REQUIRE(resource.asset);
        CHECK(std::any_of(resource.asset->interleaved_samples.begin(),resource.asset->interleaved_samples.end(),[](float v){return v!=0;}));
    }
    Sink sink;g::LocalAudio playback(sink,true);playback.update(batch,&loader,now,true);
    api::LocalAudioBatch empty;empty.session=batch.session;empty.scope=batch.scope;empty.movement_epoch=batch.movement_epoch;
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(sink.starts<batch.count && std::chrono::steady_clock::now()<deadline) {
        std::this_thread::sleep_for(1ms);playback.update(empty,&loader,now,true);
    }
    REQUIRE(sink.starts==batch.count);std::array<float,512> output{};sink.output.mixer.render(output);
    CHECK(std::all_of(output.begin(),output.end(),[](float v){return std::isfinite(v);}));
    CHECK(std::any_of(output.begin(),output.end(),[](float v){return v!=0;}));
    playback.update(batch,&loader,now,true);CHECK(sink.starts==batch.count);
}
api::LocalSoundReference sound_reference(std::string_view name) {
    api::LocalSoundReference r;r.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    std::copy(name.begin(),name.end(),r.sample.begin());return r;
}
api::ScriptedEventBinding sound_binding(std::uint16_t slot,std::string_view name) {
    api::ScriptedEventBinding b;b.index=slot;std::copy(name.begin(),name.end(),b.name.begin());return b;
}
api::LocalWeaponModelMetadata audio_mp5_model() {
    api::LocalWeaponModelMetadata m;m.generation=1;m.model_index=73;m.resource_revision=2;
    m.resource_name="models/v_9mmAR.mdl";m.supported_bodies[0]=m.supported_bodies[2]=true;
    m.selectable_bodies[0]=m.selectable_bodies[2]=true;
    for(const auto label:{"longidle","idle1","grenade","reload","deploy","shoot","shoot","shoot"})
        m.sequences.push_back({10,7,false,{},label});
    m.sequences[0].looping=m.sequences[1].looping=true;
    m.sequences[3].frame_count=47;m.sequences[3].fps=30;return m;
}
c::RuntimeClientObservationState audio_mp5_state() {
    auto s=f::state(50);s.receiving_client->owned_weapon_bits=1U<<4;
    s.weapon_hud.active_weapon_id=4;s.weapon_hud.clips[3].reset();s.weapon_hud.clips[4]=50;
    s.weapon_hud.reserve_ammo[3]=3;
    s.weapon_hud.catalogue.clear();s.weapon_hud.catalogue.push_back({.id=4,.command_name="weapon_9mmAR",
        .primary_ammo_type=1,.secondary_ammo_type=3,.slot=2,.source=*s.client_metadata.source});
    s.weapon_slots.clear();s.weapon_slots.push_back({.wire_slot=4,.clip=50,.in_reload=false,
        .next_primary_attack=0,.weapon_id=4,.next_secondary_attack=0,.time_weapon_idle=0});return s;
}
void draw_projection(app::RuntimeReplayLocalAssets& projection) {
    f::Module m;auto metadata=projection.presentation_model(1,73);REQUIRE(metadata);
    REQUIRE(hlclient::games::halflife::python_sequences(*metadata));m.host.bind_model(metadata);
    auto gl=fixture::entity_opengl_fixture::try_context(640,480);
    if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
    gl->initialize_renderer();r::RenderScene scene;
    hlclient::input::InputStateTracker tracker;app::LiveVisualCameraController camera;c::ClientWorldState camera_world;
    const auto bindings=hlclient::gameplay_input::GameplayInputBindingsBuilder{}.build({});REQUIRE(bindings);
    const auto project=[&](double degrees,double aspect) {
        tracker.begin_frame();const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
        const auto input=hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.02);
        REQUIRE(input);api::CameraIntent intent;intent.horizontal_field_of_view_degrees=degrees;
        REQUIRE(camera.update(&m.observed,*input.intent,camera_world,intent,false,{},aspect));
        scene.camera.vertical_field_of_view_radians=camera_world.camera().vertical_field_of_view_radians;
    };
    for(const auto extent:{r::RenderExtent{320,240},r::RenderExtent{640,360},r::RenderExtent{640,400}}) {
        const double aspect=static_cast<double>(extent.width)/extent.height;
        const auto draw=[&]{gl->renderer().render(scene,extent);CHECK(glGetError()==GL_NO_ERROR);
            return gl->renderer().observe_framebuffer(extent,scene.clear_color,true);};
        api::ViewmodelIntent pose;pose.status=api::ViewmodelStatus::ready;pose.generation=pose.publication_revision=1;
        pose.model_index=73;pose.sequence=0;pose.body=1;
        scene.first_person_entities=projection.materialize_viewmodel(pose);REQUIRE(scene.first_person_entities);
        scene.camera.position={0,0,0};scene.camera.target={1,0,0};
        project(90,aspect);
        const auto normal=draw();REQUIRE(normal);CHECK(normal.non_clear_pixel_count>0);
        const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
        project(40,aspect);
        const auto zoom=draw();REQUIRE(zoom);CHECK_FALSE(std::ranges::equal(normal.rgba8,zoom.rgba8));
        CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
        project(90,aspect);
        CHECK(std::ranges::equal(draw().rgba8,normal.rgba8));
        pose.body=0;scene.first_person_entities=projection.materialize_viewmodel(pose);
        CHECK_FALSE(std::ranges::equal(draw().rgba8,normal.rgba8)); // laser/alternate geometry
        pose.body=1;pose.sequence=2;pose.frame_coordinate=10;
        scene.first_person_entities=projection.materialize_viewmodel(pose);const auto fire=draw();REQUIRE(fire);
        CHECK_FALSE(std::ranges::equal(fire.rgba8,normal.rgba8));
        scene.camera.position={100,-70,90};scene.camera.target={100.1F,-69.9F,91};
        CHECK(std::ranges::equal(draw().rgba8,fire.rgba8)); // A1 unchanged at pitch/yaw
        CHECK(r::camera_local_first_person_camera(scene.camera).near_plane==1.0F);
        CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    }
    gl->release_renderer();
}
}
TEST_CASE("Python and both MP5 modes retain local and observer PCM after sixty-four loaded client samples",
          "[e18][e15][e16][weapon-audio][audio-capacity][no-stock]") {
    Owned owned;g::ApprovedSoundAssets loader(owned.projection->sound_resources());
    for(const auto token:{"hks1","hks2","glauncher","glauncher2"})
        owned.root.write("valve",std::string{"sound/weapons/"}+token+".wav",wav());
    // Project-owned ready samples fill the old quota before the weapon switch.
    // These are not playing voices; the regression is loader admission itself.
    for(unsigned i=0;i<64;++i) {
        const auto name="weapons/cache_fixture_"+std::to_string(i)+".wav";
        owned.root.write("valve","sound/"+name,wav());const auto reference=sound_reference(name);
        auto loaded=loader.request_local(reference);const auto deadline=std::chrono::steady_clock::now()+2s;
        while(loaded.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms);loaded=loader.request_local(reference);
        }
        REQUIRE(loaded.status==g::SoundAssetStatus::ready);
    }
    const std::array bindings{f::binding(),sound_binding(928,"events/mp5.sc"),sound_binding(929,"events/mp52.sc")};
    for(unsigned mode=0;mode<3;++mode) for(bool observer:{false,true}) {
        CAPTURE(mode,observer);
        api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
        host.reset({1,1,1});host.set_movement_audio_time_origin(0);
        const auto state=mode ? audio_mp5_state() : f::state();
        api::GameRecordState record;record.multiplayer.maximum_clients=8;record.weapon_hud=state.weapon_hud;
        host.commit_record(std::move(record));host.bind_model(mode ? audio_mp5_model() : f::model());
        host.observe(state,0);host.configure_scripted_events(bindings);(void)host.drain_audio();
        api::LocalAudioBatch batch;
        if(observer) {
            auto event=f::event(2,10+mode,.02);event.event_index=bindings[mode].index;
            host.committed_scripted_event(event);(void)host.drain_remote_effects(.02);
            batch=host.drain_remote_audio();host.committed_scripted_event(event);
            (void)host.drain_remote_effects(.02);CHECK(host.drain_remote_audio().count==0);
        } else {
            const auto command=f::command(1,.02,mode==2 ? g::kReferenceGoldSrcButtonAttack2 : g::kReferenceGoldSrcButtonAttack);
            host.submit(command,.02);const auto shown=host.sample(.02);
            CHECK((mode==2 ? shown.secondary_launch_starts : shown.primary_fire_starts)==1);
            batch=host.drain_audio();host.submit(command,.02);(void)host.sample(.02);
            CHECK(host.drain_audio().count==0);
        }
        REQUIRE(batch.count==1);const auto sample=batch.cues[0].reference.name();
        CHECK((mode==0 ? sample=="weapons/357_shot1.wav" || sample=="weapons/357_shot2.wav" :
               mode==1 ? sample=="weapons/hks1.wav" || sample=="weapons/hks2.wav" :
                         sample=="weapons/glauncher.wav" || sample=="weapons/glauncher2.wav"));
        pcm(loader,batch,.02); // Isolated real decode -> production delivery -> mixer, duplicate control.
    }
}
TEST_CASE("Complete Half-Life client audio preparation fits the approved bounded cache",
          "[weapon-audio][audio-capacity][game-api][no-stock]") {
    Owned owned;f::Module module;
    const std::array names{"events/glock1.sc","events/glock2.sc","events/crowbar.sc",
        "events/mp5.sc","events/mp52.sc","events/shotgun1.sc","events/shotgun2.sc",
        "events/python.sc","events/crossbow1.sc","events/crossbow2.sc","events/rpg.sc",
        "events/gauss.sc","events/gaussspin.sc"};
    std::vector<api::ScriptedEventBinding> bindings;
    for(std::size_t i=0;i<names.size();++i) bindings.push_back(sound_binding(static_cast<std::uint16_t>(900+i),names[i]));
    module.host.configure_scripted_events(bindings);
    std::set<std::string> samples;
    const auto add=[&](const api::LocalSoundReference& reference){samples.emplace(reference.name());};
    for(const auto& reference:module.host.movement_sound_preparation()) add(reference);
    for(const auto& reference:module.host.temporary_sound_preparation()) add(reference);
    const auto impact=module.host.local_impact_assets();REQUIRE(impact);
    add(impact->sound);add(impact->shell_contact_sound);
    for(std::size_t i=0;i<impact->material_sound_count;++i) add(impact->material_sounds[i]);
    for(std::size_t i=0;i<impact->supplemental_sound_count;++i) add(impact->supplemental_sounds[i]);
    const auto crowbar=module.host.local_crowbar_impact_assets();REQUIRE(crowbar);
    for(const auto& reference:crowbar->strike_sounds) add(reference);
    add(crowbar->concrete_contact_sound);
    for(const auto& batch:std::array{module.host.drain_audio(),module.host.drain_remote_audio()})
        for(std::size_t i=0;i<batch.prepare_count;++i) add(batch.prepare[i]);
    CAPTURE(samples.size());CHECK(samples.size()>64);CHECK(samples.size()<=g::kMaximumApprovedLocalSoundEntries);
    std::cout << "halflife_audio_profile_unique_resources=" << samples.size() << '\n';
    for(const auto& name:samples) owned.root.write("valve","sound/"+name,wav());
    g::ApprovedSoundAssets loader(owned.projection->sound_resources());
    for(const auto& name:samples) {
        INFO(name);const auto reference=sound_reference(name);auto ready=loader.request_local(reference);
        const auto deadline=std::chrono::steady_clock::now()+2s;
        while(ready.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms);ready=loader.request_local(reference);
        }
        REQUIRE(ready.status==g::SoundAssetStatus::ready);REQUIRE(ready.asset);
    }
}
TEST_CASE("E18 owned approved importer body fire zoom GPU reuse and isolated shot PCM", "[e18][composition][opengl][weapon-audio]") {
    Owned owned;draw_projection(*owned.projection);
    f::Module m;auto meta=owned.projection->presentation_model(1,73);REQUIRE(meta);m.host.bind_model(meta);
    (void)m.host.drain_audio();m.host.submit(f::command(),.02);(void)m.host.sample(.02);
    const auto audio=m.host.drain_audio();REQUIRE(audio.count==1);
    g::ApprovedSoundAssets loader(owned.projection->sound_resources());pcm(loader,audio,.02);
    auto result=m.host.viewmodel(m.observed,meta,.02,m.host.sample(.02).visual);
    REQUIRE(owned.projection->materialize_viewmodel(result));REQUIRE(owned.projection->viewmodel_attachment_zero());
    const auto visuals=m.host.local_visuals({{}, {1,0,0}, {0,-1,0}, {0,0,1}, {}, .02});
    REQUIRE(visuals.flash);REQUIRE(visuals.light);CHECK_FALSE(visuals.shell);
    auto gl=fixture::entity_opengl_fixture::try_context(320,240);REQUIRE(gl);gl->initialize_renderer();
    r::RenderScene scene;scene.first_person_entities=owned.projection->materialize_viewmodel(result);
    const auto draw=[&]{gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);};
    const auto baseline=draw();REQUIRE(baseline);
    const auto muzzle=*owned.projection->viewmodel_attachment_zero();
    scene.first_person_flash=r::RenderMuzzleFlash{muzzle,visuals.flash->radius_units,visuals.flash->color};
    CHECK_FALSE(std::ranges::equal(baseline.rgba8,draw().rgba8));scene.first_person_flash.reset();
    scene.transient_first_person_light=r::RenderPointLight{muzzle,visuals.light->radius_units,
        visuals.light->intensity,visuals.light->color};
    CHECK_FALSE(std::ranges::equal(baseline.rgba8,draw().rgba8));
    scene.transient_first_person_light.reset();CHECK(std::ranges::equal(baseline.rgba8,draw().rgba8));
    gl->release_renderer();
}
TEST_CASE("E18 optional approved actual Python model body muzzle reload audio and GPU controls", "[e18][installed-python][opengl][local-capture-assets]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count=0;
    if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;std::free(value);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("No exact opt-in approved research root");
    const auto resources=readiness::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0},
        {2,"models/v_357.mdl",73,0,0},{2,"models/p_357.mdl",71,0,0},
        {2,"models/w_357.mdl",74,0,0},{2,"models/w_357ammobox.mdl",75,0,0},{2,"models/player.mdl",7,0,0}});
    auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),
        root,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(created.projection);c::ClientWorldState world;REQUIRE(created.projection->reset_generation(initial(),world));
    const auto v=created.projection->studio_model_data(73),p=created.projection->studio_model_data(71);
    REQUIRE(v);REQUIRE(p);CHECK(v->bones.size()==66);CHECK(v->attachments.size()==1);
    CHECK(v->bodyparts.size()==5);CHECK(v->skin_families.size()==1);CHECK(p->attachments.empty());
    CHECK(p->bodyparts.size()==1);CHECK(p->bones[11].name=="Cylinder01");
    f::Module m;auto meta=created.projection->presentation_model(1,73);REQUIRE(meta);m.host.bind_model(meta);
    const auto plan=m.host.remote_weapon_socket_plan({71,{3,4},"models/p_357.mdl",*p});
    CHECK(plan.source==api::RemoteSocketSource::validated_pmodel_socket);CHECK(plan.semantic_key==3);
    draw_projection(*created.projection);
    const auto player=created.projection->studio_model_data(7);REQUIRE(player);
    const auto role=[&](std::string_view first,std::string_view second={}) {
        const auto found=std::ranges::find_if(player->sequences,[&](const auto& seq) {
            return seq.label.find(first)!=std::string::npos && (second.empty() || seq.label.find(second)!=std::string::npos);
        });
        REQUIRE(found!=player->sequences.end());return static_cast<std::uint32_t>(found-player->sequences.begin());
    };
    f::remote_gpu(*created.projection,{0,0,16},{role("walk"),role("crouch"),role("shoot","python"),role("die")});
    (void)m.host.drain_audio();m.host.submit(f::command(),.02);(void)m.host.sample(.02);
    const auto fire=m.host.drain_audio();REQUIRE(fire.count==1);
    g::ApprovedSoundAssets loader(created.projection->sound_resources());pcm(loader,fire,.02);
    for(const auto name:{"weapons/357_shot1.wav","weapons/357_shot2.wav","weapons/357_reload1.wav","weapons/357_cock1.wav"}) {
        api::LocalAudioBatch sample;sample.session=99;sample.scope=1;sample.count=1;
        sample.cues[0].scope=sample.scope;sample.cues[0].serial=1;
        sample.cues[0].reference.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;
        std::copy_n(name,std::char_traits<char>::length(name),sample.cues[0].reference.sample.begin());
        pcm(loader,sample,0);
    }
}
TEST_CASE("E18 owned normal remote p357 bone merge GPU server pose death rearm and holster", "[e18][opengl][remote-effects][composition]") {
    Owned owned;f::remote_gpu(*owned.projection,{0,0,16},{1,2,3,4});
}
TEST_CASE("E18 server zoom changes world GPU projection alone and reset restores identical pixels", "[e18][camera][opengl][composition]") {
    auto gl=fixture::entity_opengl_fixture::try_context(640,480);
    if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
    gl->initialize_renderer();auto package=fixture::world_render_fixture::make_package();REQUIRE(package);
    r::RenderScene scene;scene.static_world=r::RenderStaticWorld{
        std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*package.package))};
    scene.camera.position={8,-24,16};scene.camera.target={8,8,0};
    f::Module module;hlclient::input::InputStateTracker tracker;app::LiveVisualCameraController camera;
    c::ClientWorldState world;const auto bindings=hlclient::gameplay_input::GameplayInputBindingsBuilder{}.build({});
    REQUIRE(bindings);
    for(const auto extent:{r::RenderExtent{320,240},r::RenderExtent{640,360},r::RenderExtent{640,400}}) {
        const auto project=[&](std::uint8_t value,std::uint64_t ordinal) {
            module.observed.publication_revision=ordinal;module.observed.weapon_hud.field_of_view=value;
            module.observed.weapon_hud.field_of_view_source=c::RuntimeObservationSource{
                ordinal,static_cast<std::size_t>(ordinal),0,0,8};
            module.host.observe(module.observed,static_cast<double>(ordinal)*.02);
            tracker.begin_frame();const auto snapshot=tracker.publish_snapshot();tracker.end_frame();
            const auto input=hlclient::gameplay_input::GameplayInputIntentBuilder{}.build(snapshot,*bindings.bindings,{},.02);
            REQUIRE(input);REQUIRE(camera.update(&module.observed,*input.intent,world,
                module.host.camera(module.observed,0),false,{},static_cast<double>(extent.width)/extent.height));
            scene.camera.vertical_field_of_view_radians=world.camera().vertical_field_of_view_radians;
        };
        const auto draw=[&]{gl->renderer().render(scene,extent);CHECK(glGetError()==GL_NO_ERROR);
            return gl->renderer().observe_framebuffer(extent,scene.clear_color,true);};
        const auto ordinal=module.observed.publication_revision+1;project(0,ordinal);
        const auto baseline=draw();REQUIRE(baseline);REQUIRE(baseline.non_clear_pixel_count>0);
        const auto saved_camera=scene.camera;const auto uploads=gl->renderer().statistics().upload_count;
        project(40,ordinal+1);const auto zoom=draw();REQUIRE(zoom);
        CHECK(zoom.non_clear_pixel_count>baseline.non_clear_pixel_count*2);
        CHECK_FALSE(std::ranges::equal(zoom.rgba8,baseline.rgba8));
        CHECK(f::same(scene.camera.position,saved_camera.position));CHECK(f::same(scene.camera.target,saved_camera.target));
        CHECK(gl->renderer().statistics().upload_count==uploads);
        project(0,ordinal+2);CHECK(std::ranges::equal(draw().rgba8,baseline.rgba8));
        CHECK(scene.camera==saved_camera);CHECK(gl->renderer().statistics().upload_count==uploads);
    }
    gl->release_renderer();
}
