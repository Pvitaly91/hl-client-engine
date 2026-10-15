#include "crossbow_test_fixture.hpp"
#include "python_357_pmodel_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/crossbow_profile.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <cstdlib>
namespace f=hlclient::tests::crossbow_fixture;
namespace fixture=hlclient::tests;
namespace api=hlclient::game_api;
namespace c=hlclient::client;
namespace app=hlclient::app;
namespace g=hlclient::goldsrc;
namespace r=hlclient::renderer;
namespace readiness=fixture::readiness_fixture;
using namespace std::chrono_literals;
namespace {
c::RuntimeClientObservationState initial() {
    auto s=f::state();s.entity_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{1,1,1,0,8}};
    s.server_time_metadata=s.client_metadata=s.entity_metadata;s.server_time_seconds=0;
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);REQUIRE(c::valid_runtime_observation(s));return s;
}
std::vector<std::byte> owned_crossbow() {
    auto bytes=fixture::literal_minimal_goldsrc_studio_v10();
    const auto seqs=bytes.size(),count=f::model().sequences.size();bytes.resize(seqs+count*(176+18));
    fixture::studio_write_i32le(bytes,164,static_cast<int>(count));fixture::studio_write_i32le(bytes,168,static_cast<int>(seqs));
    fixture::studio_write_i32le(bytes,212,0);fixture::studio_write_i32le(bytes,216,0);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset,16,-4,-5);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+12,16,4,-5);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+24,16,0,3);
    const auto meta=f::model();
    for(std::size_t i=0;i<count;++i) {
        const auto at=seqs+i*176,anim=seqs+count*176+i*18;const auto& seq=meta.sequences[i];
        std::copy_n(bytes.begin()+fixture::kSyntheticStudioSequenceOffset,176,bytes.begin()+at);
        fixture::studio_write_fixed_string(bytes,at,32,seq.label);
        fixture::studio_write_f32le(bytes,at+32,static_cast<float>(seq.fps));
        fixture::studio_write_i32le(bytes,at+36,seq.looping ? 1 : 0);
        fixture::studio_write_i32le(bytes,at+56,static_cast<int>(seq.frame_count));
        fixture::studio_write_i32le(bytes,at+124,static_cast<int>(anim));
        fixture::studio_write_u16le(bytes,anim+2,12);bytes[anim+12]=std::byte{2};
        bytes[anim+13]=static_cast<std::byte>(seq.frame_count);
        fixture::studio_write_i16le(bytes,anim+14,0);fixture::studio_write_i16le(bytes,anim+16,static_cast<std::int16_t>(i));
    }
    fixture::studio_write_i32le(bytes,72,static_cast<int>(bytes.size()));return bytes;
}
std::vector<std::byte> owned_pmodel(bool weapon) {
    auto bytes=fixture::python_fixture::remote_model(weapon);
    const auto i32=[&](std::size_t at){unsigned v{};for(unsigned i=0;i<4;++i)v|=std::to_integer<unsigned>(bytes[at+i])<<(8*i);return v;};
    if(weapon) {
        const auto last=i32(144)+11*112;fixture::studio_write_fixed_string(bytes,last,32,"Line01");
        fixture::studio_write_vector3(bytes,last+64,7.571011F,1.0124F,.781353F);
        fixture::studio_write_vector3(bytes,last+76,1.543135F,.028283F,-3.045966F);
    }
    return bytes;
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
        fixture::SyntheticWad3Entry texture;texture.name="TEST_QUAD";texture.payload=fixture::synthetic_goldsrc_miptex("TEST_QUAD",64,64);
        root.write("valve","test.wad",fixture::synthetic_wad3({texture}).bytes);root.write("valve","maps/test.bsp",bsp.build());
        root.write("valve","models/v_crossbow.mdl",owned_crossbow());
        root.write("valve","models/p_crossbow.mdl",owned_pmodel(true));
        root.write("valve","models/project_player.mdl",owned_pmodel(false));
        root.write("valve","models/crossbow_bolt.mdl",fixture::literal_minimal_goldsrc_studio_v10());
        for(const auto sample:{"xbow_fire1","xbow_reload1","xbow_hit1","xbow_hitbod1","xbow_hitbod2"})
            root.write("valve",std::string{"sound/weapons/"}+sample+".wav",wav());
        const auto resources=readiness::parse_resource_list({{2,"maps/test.bsp",9,0,0},{2,"models/v_crossbow.mdl",73,0,0},
            {2,"models/p_crossbow.mdl",71,0,0},{2,"models/project_player.mdl",7,0,0},{2,"models/crossbow_bolt.mdl",74,0,0}});
        auto made=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test.bsp"),root.path(),"valve",
            app::ReplayLocalCameraPolicy::external_live_receiving_client);
        INFO((made.error ? made.error->context : "approved owned assets"));REQUIRE(made.projection);
        projection=std::move(made.projection);c::ClientWorldState world;REQUIRE(projection->reset_generation(initial(),world));
    }
};
struct Sink:hlclient::audio::Output {
    hlclient::audio::OfflineOutput output;unsigned starts{};
    bool submit(const hlclient::audio::Command& command) noexcept override {
        if(command.operation==hlclient::audio::Operation::start)++starts;return output.submit(command);
    }
    void set_listener(hlclient::audio::Listener l) noexcept override {output.set_listener(l);}
};
void mix(g::ApprovedSoundAssets& loader,api::LocalAudioBatch batch,double now) {
    for(std::size_t i=0;i<batch.count;++i) {
        auto resource=loader.request_local(batch.cues[i].reference);const auto deadline=std::chrono::steady_clock::now()+2s;
        while(resource.status==g::SoundAssetStatus::pending && std::chrono::steady_clock::now()<deadline) {
            std::this_thread::sleep_for(1ms);resource=loader.request_local(batch.cues[i].reference);
        }
        INFO(batch.cues[i].reference.name());REQUIRE(resource.status==g::SoundAssetStatus::ready);REQUIRE(resource.asset);
        CHECK(resource.asset->sample_rate>0);CHECK(resource.asset->channel_count==1);
        CHECK(std::ranges::any_of(resource.asset->interleaved_samples,[](float v){return v!=0;}));
    }
    Sink sink;sink.set_listener({{0,80,80},{0,1,0},1,false});g::LocalAudio player(sink,true);
    player.update(batch,&loader,now,true);
    auto empty=batch;empty.count=empty.prepare_count=0;
    const auto delivery_deadline=std::chrono::steady_clock::now()+2s;
    while(sink.starts<batch.count && std::chrono::steady_clock::now()<delivery_deadline) {
        std::this_thread::sleep_for(1ms);player.update(empty,&loader,now,true);
    }
    CAPTURE(batch.scope,batch.cues[0].scope,batch.cues[1].scope,batch.cues[0].serial,batch.cues[1].serial,
        batch.cues[0].scheduled_seconds,batch.cues[1].scheduled_seconds,player.statistics().duplicates,
        player.statistics().cancelled,player.statistics().limits,player.statistics().resource_pending,
        batch.cues[0].volume,batch.cues[1].volume);
    CHECK(sink.starts==batch.count);
    std::array<float,512> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
    CHECK(std::ranges::any_of(pcm,[](float v){return v!=0;}));
}
void gpu(app::RuntimeReplayLocalAssets& projection) {
    auto gl=fixture::entity_opengl_fixture::try_context(320,240);
    if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("E19 OpenGL 3.3 unavailable");
    gl->initialize_renderer();auto meta=projection.presentation_model(1,73);REQUIRE(meta);
    CHECK(hlclient::games::halflife::crossbow_sequences(*meta));
    f::Module m;m.host.bind_model(meta);m.host.submit(f::command(),.02);auto shot=m.host.sample(.02);
    auto pose=m.host.viewmodel(m.observed,meta,.04,shot.visual);REQUIRE(pose.status==api::ViewmodelStatus::ready);
    r::RenderScene scene;scene.first_person_entities=projection.materialize_viewmodel(pose);REQUIRE(scene.first_person_entities);
    const auto draw=[&]{gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
        return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);};
    const auto front=draw();REQUIRE(front);REQUIRE(front.non_clear_pixel_count>0);
    scene.camera.position={90,80,52};scene.camera.target={90,82,53};CHECK(std::ranges::equal(front.rgba8,draw().rgba8));
    scene.first_person_entities.reset();scene.camera.position={96,-64,48};scene.camera.target={0,0,0};
    const auto baseline=draw();api::TemporaryEffectsBatch batch;batch.count=1;
    auto& e=batch.effects[0];e.identity={1,8,2,3,api::TemporaryEffectIdentity::Source::game_action};
    e.model.emplace();constexpr std::string_view name="models/crossbow_bolt.mdl";
    std::copy(name.begin(),name.end(),e.model->model_name.begin());e.model->angles_degrees={0,35,0};
    app::TemporaryEffectPresentation owner;owner.reset(1);owner.consume(batch,0);
    scene.transient_world_entities=projection.materialize_shells({},0,owner.models());REQUIRE(scene.transient_world_entities);
    CHECK(scene.transient_world_entities->frame->studio_instances()[0].transform.rotation_degrees.z==35);
    const auto bolt=draw();REQUIRE(bolt.non_clear_pixel_count>0);CHECK_FALSE(std::ranges::equal(baseline.rgba8,bolt.rgba8));
    scene.camera.position={64,-96,64};scene.camera.target={0,0,0};REQUIRE(draw().non_clear_pixel_count>0);
    const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
    owner.present(scene,4.9);scene.transient_world_entities=projection.materialize_shells({},4.9,owner.models());REQUIRE(scene.transient_world_entities);
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    scene.camera.position={96,-64,48};scene.camera.target={0,0,0};owner.present(scene,5);
    scene.transient_world_entities=projection.materialize_shells({},5,owner.models());CHECK_FALSE(scene.transient_world_entities);
    CHECK(std::ranges::equal(baseline.rgba8,draw().rgba8));gl->release_renderer();
}
}
TEST_CASE("E19 owned approved resources normal host launch mechanical and isolated impact decode mix", "[e19][composition][weapon-audio][no-stock]") {
    Owned owned;f::Module m;const auto meta=owned.projection->presentation_model(1,73);REQUIRE(meta);
    m.host.bind_model(meta);m.zoom();(void)m.host.drain_audio();m.host.submit(f::command(),.02);(void)m.host.sample(.02);
    auto fire=m.host.drain_audio();REQUIRE(fire.count==2);g::ApprovedSoundAssets loader(owned.projection->sound_resources());mix(loader,fire,.02);
    const auto request=m.host.local_visuals({{}, {}, {}, {}, {}, .02});REQUIRE(request.world_impact);
    m.host.accepted_world_impact(request.world_impact->action,{0,80,80},.03);
    const auto hit=m.host.drain_audio();REQUIRE(hit.count==1);CHECK(hit.cues[0].reference.name()=="weapons/xbow_hit1.wav");mix(loader,hit,.03);
}
TEST_CASE("E19 owned normal viewmodel and fixed bolt use production OpenGL pass without projectile physics", "[e19][opengl][composition][no-stock]") {Owned owned;gpu(*owned.projection);}
TEST_CASE("E19 remote pCrossbow uses existing named bone merge and server sequence with actual GPU coverage", "[e19][remote-effects][opengl][no-stock]") {
    Owned owned;f::Module m;c::ClientWorldState world;world.set_camera({{96,-64,48},{0,0,16},{0,0,1}});
    auto s=initial();s.server_time_seconds=1;s.publication_revision=10;
    s.entity_metadata.source=c::RuntimeObservationSource{10,10,0,0,8};
    s.server_time_metadata=s.client_metadata=s.entity_metadata;
    c::RuntimePacketEntityObservation e;e.entity_number=2;e.ordinary_visual_schema=e.player_movement_schema=true;
    e.model_index=7;e.weapon_model_index=71;e.origin={0,0,16};e.angles={0,0,0};e.sequence=1;e.frame=0;
    e.animation_time_seconds=1;e.frame_rate=1;e.body=e.skin=e.gait_sequence=0;e.controllers.fill(127);e.blending.fill(127);
    s.packet_entities={e};s.canonical_state_hash=c::runtime_observation_canonical_hash(s);REQUIRE(c::valid_runtime_observation(s));
    REQUIRE(owned.projection->reset_generation(s,world));REQUIRE(owned.projection->present_entities(m.host,99,1,world,{320,240}));
    REQUIRE(owned.projection->present_entities(m.host,99.21,1,world,{320,240}));
    REQUIRE(world.entity_frame());const auto frame=world.entity_frame();
    CAPTURE(owned.projection->summary().remote_player_count);
    if(owned.projection->summary().remote_player_count) {
        CAPTURE(static_cast<int>(owned.projection->summary().remote_players[0].status));
    }
    REQUIRE(frame->studio_instances().size()==2);
    const auto& parent=frame->studio_instances()[0];const auto& child=frame->studio_instances()[1];
    const auto& p=frame->studio_poses()[parent.pose_index].bone_matrices;const auto& w=frame->studio_poses()[child.pose_index].bone_matrices;
    REQUIRE(p.size()==12);REQUIRE(w.size()==12);for(std::size_t bone=0;bone<11;++bone)CHECK(p[bone]==w[bone]);
    CHECK(s.packet_entities[0].sequence==1);CHECK(child.body_value==0);
    auto gl=fixture::entity_opengl_fixture::try_context(320,240);REQUIRE(gl);gl->initialize_renderer();
    auto scene=c::build_render_scene(world);scene.static_world.reset();REQUIRE(scene.dynamic_entities);
    gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
    const auto seen=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(seen.non_clear_pixel_count>0);
    s.publication_revision=11;s.entity_metadata.source=c::RuntimeObservationSource{11,11,0,0,8};s.server_time_metadata=s.client_metadata=s.entity_metadata;
    s.server_time_seconds=1.1;s.packet_entities[0].weapon_model_index=0;s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
    REQUIRE(owned.projection->project_entities(s,world));REQUIRE(owned.projection->present_entities(m.host,100,1,world,{320,240}));
    REQUIRE(world.entity_frame());CHECK(world.entity_frame()->studio_instances().size()==1);gl->release_renderer();
}
TEST_CASE("E19 normal server MOVETYPE_FLY bolt smooths committed origins and obeys removal slot reuse", "[e19][projectile][composition][no-stock]") {
    Owned owned;f::Module m;c::ClientWorldState world;world.set_camera({{96,-64,48},{0,0,16},{0,0,1}});
    const auto state=[](std::uint64_t ordinal,double time,double y,std::uint32_t model=74) {
        auto s=initial();s.publication_revision=ordinal;s.server_time_seconds=time;
        s.entity_metadata.source=c::RuntimeObservationSource{ordinal,static_cast<std::size_t>(ordinal),0,0,8};
        s.server_time_metadata=s.client_metadata=s.entity_metadata;
        c::RuntimePacketEntityObservation e;e.entity_number=40;e.ordinary_visual_schema=true;e.model_index=model;
        e.brush_move_type=5;e.origin={0,y,24};e.angles={20,35,0};e.body=e.skin=e.sequence=0;e.frame=0;
        s.packet_entities={e};s.canonical_state_hash=c::runtime_observation_canonical_hash(s);REQUIRE(c::valid_runtime_observation(s));return s;
    };
    const auto present=[&](double at){REQUIRE(owned.projection->present_entities(m.host,at,1,world,{320,240}));};
    auto first=state(1,1,0);REQUIRE(owned.projection->reset_generation(first,world));present(99);
    REQUIRE(world.entity_frame());REQUIRE(world.entity_frame()->studio_instances().size()==1);
    CHECK(world.entity_frame()->studio_instances()[0].entity_number==40);
    // Pinned air speed gives a 200-unit authoritative interval, greater than
    // the independent player teleport guard. No client velocity is simulated.
    const auto second=state(2,1.1,200);REQUIRE(owned.projection->project_entities(second,world));present(99.1);present(99.15);
    const auto& mid=world.entity_frame()->studio_instances()[0];CHECK(mid.transform.origin.y==Catch::Approx(100).margin(.002));
    // Neutral renderer uses roll/X, -pitch/Y, yaw/Z, not SDK pitch/yaw/roll.
    CHECK(mid.transform.rotation_degrees.x==0);CHECK(mid.transform.rotation_degrees.y==-20);
    CHECK(mid.transform.rotation_degrees.z==35);
    CHECK(second.packet_entities[0].origin.y==200);CHECK(second.canonical_state_hash==c::runtime_observation_canonical_hash(second));
    m.observed=f::state(5,2);m.observed.receiving_client->health=0;m.host.observe(m.observed,.2);
    present(99.17);CHECK(world.entity_frame()->studio_instances().size()==1);
    CHECK(m.host.hud(m.observed,.2).health==0); // player death does not delete the server-owned ordinary bolt
    auto removed=state(3,1.2,200);removed.packet_entities.clear();removed.canonical_state_hash=c::runtime_observation_canonical_hash(removed);
    REQUIRE(owned.projection->project_entities(removed,world));present(100);
    CHECK(world.entity_frame()->studio_instances().empty());
    auto reused=state(4,1.3,40,73);REQUIRE(owned.projection->project_entities(reused,world));present(101);
    REQUIRE(world.entity_frame()->studio_instances().size()==1);CHECK(world.entity_frame()->studio_instances()[0].transform.origin.y==40);
    CHECK(m.host.drain_temporary_effects(101).count==0);CHECK(m.host.drain_audio().count==0);
}
TEST_CASE("E19 optional read-only approved actual Crossbow models audio and GPU", "[e19][installed-crossbow][opengl][local-capture-assets]") {
    std::filesystem::path root;
#ifdef _WIN32
    wchar_t* value=nullptr;std::size_t count=0;
    if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;std::free(value);
#endif
    if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
        SKIP("No exact opt-in approved research root");
    const auto resources=readiness::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0},{2,"models/v_crossbow.mdl",73,0,0},
        {2,"models/p_crossbow.mdl",71,0,0},{2,"models/crossbow_bolt.mdl",74,0,0},
        {2,"models/w_crossbow.mdl",75,0,0},{2,"models/w_crossbow_clip.mdl",76,0,0}});
    auto made=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),root,"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(made.projection);
    c::ClientWorldState world;REQUIRE(made.projection->reset_generation(initial(),world));
    const auto v=made.projection->studio_model_data(73),p=made.projection->studio_model_data(71),bolt=made.projection->studio_model_data(74);
    REQUIRE(v);REQUIRE(p);REQUIRE(bolt);CHECK(v->bones.size()==56);CHECK(v->sequences.size()==12);CHECK(v->attachments.empty());
    CHECK(p->bones.size()==12);CHECK(p->bones[11].name=="Line01");CHECK(p->attachments.empty());
    CHECK(bolt->bones.size()==1);CHECK(bolt->sequences.size()==1);CHECK(bolt->attachments.empty());
    REQUIRE(made.projection->studio_model_data(75));REQUIRE(made.projection->studio_model_data(76));
    const auto meta=made.projection->presentation_model(1,73);REQUIRE(meta);CHECK(hlclient::games::halflife::crossbow_sequences(*meta));
    for(const auto& seq:meta->sequences)CHECK(seq.events.empty());
    gpu(*made.projection);g::ApprovedSoundAssets loader(made.projection->sound_resources());
    for(const auto token:{"weapons/xbow_fire1.wav","weapons/xbow_reload1.wav","weapons/xbow_hit1.wav",
        "weapons/xbow_hitbod1.wav","weapons/xbow_hitbod2.wav"}) {
        api::LocalAudioBatch b;b.session=99;b.scope=1;b.count=1;b.cues[0].scope=1;b.cues[0].serial=1;
        b.cues[0].reference=hlclient::games::halflife::crossbow_sound(token);mix(loader,b,0);
    }
}
