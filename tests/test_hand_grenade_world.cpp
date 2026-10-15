#include "hand_grenade_world_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "delta_test_fixture.hpp"
#include "player_origin_test_fixture.hpp"
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <catch2/catch_approx.hpp>

namespace {
namespace t=hlclient::tests;namespace f=t::hand_grenade_world_fixture;
namespace c=hlclient::client;namespace app=hlclient::app;namespace g=hlclient::goldsrc;
namespace api=hlclient::game_api;namespace r=hlclient::renderer;
using Catch::Approx;
std::size_t changed(const std::vector<std::byte>& a,const std::vector<std::byte>& b) {
    REQUIRE(a.size()==b.size());std::size_t n=0;
    for(std::size_t i=0;i<a.size();i+=4)if(a[i]!=b[i]||a[i+1]!=b[i+1]||a[i+2]!=b[i+2])++n;return n;
}
std::vector<std::byte> encoded_sound(unsigned entity,unsigned slot,int y,unsigned volume=64) {
    std::vector<std::byte> bytes{std::byte{6}};std::size_t bit=8;
    const auto put=[&](unsigned value,unsigned count) {
        for(unsigned i=0;i<count;++i,++bit) {
            if(bit/8>=bytes.size())bytes.push_back(std::byte{});
            if(value&(1U<<i))bytes[bit/8]|=static_cast<std::byte>(1U<<(bit%8));
        }
    };
    put(3,9);put(volume,8);put(51,8);put(2,3);put(entity,11);put(slot,8);
    put(0,1);put(1,1);put(0,1);put(1,1);put(0,1);put(y<0,1);put(static_cast<unsigned>(std::abs(y)),12);
    return bytes;
}
}

TEST_CASE("E24 timed grenade uses exact assigned w-grenade model without contact model fallback",
          "[e24][grenade-world][resources][no-stock]") {
    for(const unsigned slot:{74U,237U})for(const bool available:{false,true}) {
        t::ScopedLocalResourceTestRoot root;auto assets=f::owned(root,slot,available);auto s=f::state(slot);
        c::ClientWorldState world;REQUIRE(assets->reset_generation(s,world));
        CHECK(world.entity_frame()->studio_instances().size()==(available ? 1U : 0U));
        const auto contact=assets->studio_model_data(312);REQUIRE(contact);CHECK(contact->sequences[0].label=="owned_contact");
        const auto timed=assets->studio_model_data(slot);CHECK(bool(timed)==available);
        if(timed)CHECK(timed->sequences[0].label=="owned_timed");
        s.packet_entities[0].model_index=312;f::stamp(s,2,1.1);f::project(*assets,s,world);
        CHECK(world.entity_frame()->studio_instances().size()==1);
        s.packet_entities[0].model_index=999;f::stamp(s,3,1.2);f::project(*assets,s,world);
        CHECK(world.entity_frame()->studio_instances().empty());
    }
}

TEST_CASE("E24 authoritative BOUNCE arc sharp contact rest and reuse remain bounded without grenade physics",
          "[e24][grenade-world][composition][entity-interpolation][lifecycle][no-stock]") {
    t::ScopedLocalResourceTestRoot root;auto assets=f::owned(root);auto s=f::state();
    s.packet_entities[0].origin={0,0,20};s.packet_entities[0].angles={10,350,0};
    auto second=s.packet_entities[0];second.entity_number=41;second.origin={0,100,20};s.packet_entities.push_back(second);
    c::ClientWorldState world;f::stamp(s,1,1);REQUIRE(assets->reset_generation(s,world));
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});
    host.set_movement_audio_time_origin(0);host.observe(s,0);
    const auto present=[&](double now) {const auto result=assets->present_entities(host,now,1,world,{320,240});
        INFO((result.error ? result.error->context : "ready"));REQUIRE(result);};
    present(99);
    s.packet_entities[0].origin={20,0,0};s.packet_entities[0].angles={30,10,20};s.packet_entities[0].frame=128;
    s.packet_entities[1].origin={40,100,10};f::stamp(s,2,1.1);f::project(*assets,s,world);
    const auto immutable=s;present(99.1);present(99.15);
    auto frame=world.entity_frame();REQUIRE(frame->studio_instances().size()==2);
    CHECK(frame->studio_instances()[0].transform.origin.x==Approx(10).margin(.002));
    CHECK(frame->studio_instances()[0].transform.origin.z==Approx(10).margin(.002));
    CHECK(frame->studio_instances()[0].transform.rotation_degrees.y==Approx(-20).margin(.002));
    CHECK(std::remainder(frame->studio_instances()[0].transform.rotation_degrees.z,360.0F)==Approx(0).margin(.002));
    CHECK(s==immutable);
    // The next authoritative point reverses x and z. Linear bounded sampling
    // cannot curve through the ground or extrapolate a gravity/bounce model.
    s.packet_entities[0].origin={10,0,10};s.packet_entities[0].angles={70,50,60};s.packet_entities[0].frame_rate=.5;
    s.packet_entities[1].origin={60,100,0};f::stamp(s,3,1.2);f::project(*assets,s,world);
    present(99.2);present(99.25);frame=world.entity_frame();
    CHECK(frame->studio_instances()[0].transform.origin.x==Approx(15).margin(.002));
    CHECK(frame->studio_instances()[0].transform.origin.z==Approx(5).margin(.002));
    present(100.5);frame=world.entity_frame();CHECK(frame->studio_instances()[0].transform.origin.x==10);
    CHECK(frame->studio_instances()[0].transform.origin.z==10); // no local fuse expiry or extrapolation
    auto dead=s;dead.lifecycle.life_epoch=2;dead.lifecycle.state=c::LocalPlayerLifeState::dead;
    dead.receiving_client->health=0;host.observe(dead,1.6);host.bind_model({});host.cancel_uncommitted();present(100.6);
    CHECK(world.entity_frame()->studio_instances().size()==2);
    s.packet_entities[0].model_index=0;f::stamp(s,4,1.3);f::project(*assets,s,world);present(100.7);
    REQUIRE(world.entity_frame()->studio_instances().size()==1);CHECK(world.entity_frame()->studio_instances()[0].entity_number==41);
    s.packet_entities[1].effects=128;f::stamp(s,5,1.4);f::project(*assets,s,world);present(100.8);
    CHECK(world.entity_frame()->studio_instances().empty());
    s.packet_entities.clear();f::stamp(s,6,1.5);f::project(*assets,s,world);present(100.9);
    s=f::state(312);s.packet_entities[0].origin={300,300,10};f::stamp(s,7,1.6);f::project(*assets,s,world);present(101);
    REQUIRE(world.entity_frame()->studio_instances().size()==1);CHECK(world.entity_frame()->studio_instances()[0].transform.origin.x==300);
    CHECK(host.drain_audio().count==0);CHECK(host.drain_remote_audio().count==0);
    CHECK(host.drain_remote_effects(2).count==0);CHECK(host.drain_temporary_effects(2).count==0);
    CHECK(host.drain_follow_events().count==0);
}

TEST_CASE("E24 ordinary grenade GPU model hide server explosion smoke and expiry use existing production paths",
          "[e24][grenade-world][opengl][temporary-effects][no-stock]") {
    auto gl=t::entity_opengl_fixture::try_context();if(!gl)SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());gl->initialize_renderer();
    t::ScopedLocalResourceTestRoot root;auto assets=f::owned(root);auto s=f::state();c::ClientWorldState world;
    REQUIRE(assets->reset_generation(s,world));
    const auto draw=[&](r::RenderScene scene) {scene.clear_color={0,0,0,1};scene.camera={{0,0,20},{0,0,0},{0,1,0},1.04719755F,.1F,128};
        gl->renderer().render(scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);return t::entity_opengl_fixture::framebuffer();};
    const auto baseline=draw({});r::RenderScene scene;
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    const auto visible=draw(scene);CHECK(changed(visible,baseline)>4);
    const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
    s.packet_entities[0].angles.y=75;s.packet_entities[0].frame=128;f::stamp(s,2,1.1);f::project(*assets,s,world);
    scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    CHECK(changed(draw(scene),visible)>4);CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    s.packet_entities[0].model_index=0;s.packet_entities[0].effects=128;f::stamp(s,3,1.2);f::project(*assets,s,world);
    REQUIRE(world.entity_frame()->studio_instances().empty());scene.dynamic_entities.reset();CHECK(draw(scene)==baseline);
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});host.set_movement_audio_time_origin(0);
    app::TemporaryEffectPresentation effects;effects.reset(1);
    const auto explosion=f::effect(3,0);host.committed_temporary_effect(explosion);auto batch=host.drain_temporary_effects(0);
    REQUIRE(batch.count==1);REQUIRE(batch.effects[0].sound);effects.consume(batch,0);
    effects.present(scene,0);scene.transient_sprite_entities=assets->materialize_temporary_sprites(effects.sprites(),0);
    REQUIRE(scene.transient_sprite_entities);CHECK(changed(draw(scene),baseline)>4);
    const auto sprite_uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;
    const auto repeat=draw(scene);CHECK(draw(scene)==repeat);CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==sprite_uploads);
    host.committed_temporary_effect(explosion);CHECK(host.drain_temporary_effects(0).count==0);
    // Nothing starts a delayed smoke until the separate server message arrives.
    CHECK(host.drain_temporary_effects(.3).count==0);host.committed_temporary_effect(f::effect(4,.3,true));
    batch=host.drain_temporary_effects(.3);REQUIRE(batch.count==1);CHECK_FALSE(batch.effects[0].sound);
    effects.consume(batch,.3);scene={};effects.present(scene,.3);
    scene.transient_sprite_entities=assets->materialize_temporary_sprites(effects.sprites(),.3);
    REQUIRE(scene.transient_sprite_entities);CHECK(changed(draw(scene),baseline)>4);
    scene={};effects.present(scene,5);scene.transient_sprite_entities=assets->materialize_temporary_sprites(effects.sprites(),5);
    CHECK_FALSE(scene.transient_sprite_entities);CHECK(scene.world_particle_count==0);CHECK(scene.world_effect_light_count==0);
    CHECK(draw(scene)==baseline);CHECK_FALSE(scene.follow_trail_entities);CHECK_FALSE(scene.first_person_flash);
    GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);CHECK(depth==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    gl->release_renderer();
}

TEST_CASE("E24 committed grenade bounce and debris sounds are independent spatial one shots without collision inference",
          "[e24][grenade-world][audio][runtime-replay][no-stock]") {
    using namespace std::chrono_literals;
    auto fixture=g::make_runtime_replay_fixture({});REQUIRE(fixture);
    auto queue=std::make_shared<g::CommittedSoundQueue>();fixture.fixture->initialization.sound_events=queue;
    c::ClientWorldState world;auto replay=g::RuntimeReplaySession::initialize(fixture.fixture->initialization,world);REQUIRE(replay);
    auto record=fixture.fixture->records.front();record.received_at=g::SoundTime{};
    for(const auto entity:{40U,41U}) {const auto bytes=encoded_sound(entity,entity==40 ? 51 : 63,entity==40 ? -100 : 100,entity==40 ? 64 : 140);
        record.payload.bytes.insert(record.payload.bytes.end(),bytes.begin(),bytes.end());}
    auto malformed=record;malformed.payload.bytes.push_back(std::byte{255});const auto before=world.runtime_observation();
    CHECK_FALSE(replay.session->apply_record(malformed));CHECK(world.runtime_observation()==before);
    g::CommittedSound committed;CHECK_FALSE(queue->pop(committed));REQUIRE(replay.session->apply_record(record));
    std::array<g::CommittedSound,2> sounds;REQUIRE(queue->pop(sounds[0]));REQUIRE(queue->pop(sounds[1]));CHECK_FALSE(queue->pop(committed));
    CHECK_FALSE(replay.session->apply_record(record));CHECK_FALSE(queue->pop(committed));
    for(const auto& e:sounds) { // Isolated PCM: the other grenade cannot mask silence.
        f::Samples samples;f::Sink sink;g::ServerAudio playback(sink);
        const hlclient::audio::Listener listener{{},{0,-1,0},1,false};
        const auto y=e.sound.entity_reference==40 ? -100.F : 100.F;
        const std::array<g::SoundSource,1> alive{{{e.sound.entity_reference,{0,y,0},7}}};
        playback.present(alive,listener,{},1);CHECK(sink.starts.empty());
        playback.consume(e,{});playback.update(&samples,{});REQUIRE(sink.starts.size()==1);
        CHECK(sink.starts[0].origin.y==y);CHECK_FALSE(sink.starts[0].local);
        std::array<float,64> pcm{};sink.output.mixer.render(pcm);
        CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
        CHECK(pcm[y<0 ? 0 : 1]==0);CHECK(pcm[y<0 ? 1 : 0]>.001F);
        playback.present({},listener,g::SoundTime{}+10ms,1);
        const std::array<g::SoundSource,1> reused{{{e.sound.entity_reference,{0,-y,0},8}}};
        playback.present(reused,listener,g::SoundTime{}+20ms,1);
        std::array<float,64> after{};sink.output.mixer.render(after);CHECK(after==pcm);
        playback.consume(e,g::SoundTime{}+20ms);playback.update(&samples,g::SoundTime{}+20ms);CHECK(sink.starts.size()==1);
    }
    f::Samples samples;f::Sink sink;g::ServerAudio playback(sink);
    playback.present({},{{},{0,-1,0},1,false},{},1);
    for(const auto& e:sounds){playback.consume(e,{});playback.update(&samples,{});}
    REQUIRE(sink.starts.size()==2);CHECK(sink.starts[0].voice!=sink.starts[1].voice);
    std::array<float,64> pcm{};sink.output.mixer.render(pcm);CHECK(pcm[0]>.001F);CHECK(pcm[1]>.001F);
    CHECK(sink.output.mixer.active()==2);playback.reset(2);sink.output.mixer.render(pcm);
    CHECK(std::ranges::all_of(pcm,[](float v){return v==0;}));
}

TEST_CASE("E24 silent common-null named weapon channel replaces only its emitter without audible null effect",
          "[e24][grenade-world][audio][no-stock]") {
    f::Samples samples;f::Sink sink;g::ServerAudio playback(sink);
    playback.present({},{{},{0,-1,0},1,false},{},1);
    auto first=f::sound(1,1,51,0,1);auto other=f::sound(2,2,51,0,1);
    playback.consume(first,{});playback.update(&samples,{});playback.consume(other,{});playback.update(&samples,{});
    std::array<float,64> both{};sink.output.mixer.render(both);CHECK(both[0]>.001F);
    const auto null=f::sound(3,1,70,0,1);playback.consume(null,{});playback.update(&samples,{});
    std::array<float,64> remaining{};sink.output.mixer.render(remaining);
    CHECK(remaining[0]==Approx(both[0]*.5));CHECK(remaining[1]==Approx(both[1]*.5));
    CHECK(playback.statistics().stopped==1);REQUIRE(sink.starts.size()==3);CHECK(sink.starts.back().asset==samples.silent);
    auto second_null=f::sound(4,2,70,0,1);playback.consume(second_null,{});playback.update(&samples,{});
    sink.output.mixer.render(remaining);CHECK(std::ranges::all_of(remaining,[](float v){return v==0;}));
}

TEST_CASE("E24 remote p-grenade follows server player pose and attachment without fabricated prime or fire event",
          "[e24][grenade-world][remote-player][pmodel][lifecycle][no-stock]") {
    hlclient::assets::SkeletalModelAssetData model;
    constexpr std::array names{"Bip01","Bip01 Pelvis","Bip01 Spine","Spine1","Spine2","Spine3","Leg"};
    constexpr std::array parents{-1,0,1,2,3,4,1};
    for(std::size_t i=0;i<names.size();++i) {
        hlclient::assets::ModelBone bone;bone.name=names[i];bone.parent_index=parents[i];
        if(i>=2 && i<6)bone.controller_indices[5]=static_cast<std::int32_t>(i-2);model.bones.push_back(bone);
    }
    for(std::int32_t i=0;i<4;++i)model.bone_controllers.push_back({i+2,0x20,-30,30,127,i,false});
    for(unsigned i=0;i<4;++i) {
        hlclient::assets::ModelSequence seq;seq.label="owned_player_sequence";seq.frame_count=11;seq.frames_per_second=10;
        seq.source_flags=i==3 ? 0 : 1;seq.blend_count=1;seq.blend_start={-45,0};seq.blend_end={45,0};
        if(i==1 || i==2)seq.linear_movement.x=120;model.sequences.push_back(seq);
    }
    model.skin_families.push_back({{0}});
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});host.set_movement_audio_time_origin(0);
    c::RuntimePacketEntityObservation player;player.entity_number=2;player.player_movement_schema=player.ordinary_visual_schema=true;
    player.model_index=7;player.weapon_model_index=71;player.sequence=0;player.gait_sequence=0;player.frame=0;player.frame_rate=1;
    player.origin={40,20,12};player.angles={0,160,0};player.body=player.skin=0;player.controllers.fill(127);player.blending.fill(127);
    const auto present=[&](std::uint64_t ordinal,double time,bool discontinuity=false) {
        player.animation_time_seconds=time;const std::array entities{player};host.remote_entities(entities,8,ordinal);
        api::RemotePlayerPresentationContext context{1,1,1,44,{1,2},model,player,player,nullptr,8,1,ordinal,0,time,time,time,discontinuity,ordinal,0};
        return host.remote_player(context);
    };
    for(unsigned action=0;action<3;++action) {
        player.sequence=action;player.gait_sequence=action==1 ? 1U : 0U;
        const auto pose=present(action+1,1+.1*action);REQUIRE(pose.status==api::RemotePlayerPresentationStatus::ready);
        CHECK(pose.attachment_model_index==71);CHECK(pose.sample.sequence==action);
        CHECK(host.drain_remote_effects(1+.1*action).count==0);CHECK(host.drain_remote_audio().count==0);
        CHECK(host.drain_follow_events().count==0);CHECK(host.drain_temporary_effects(1+.1*action).count==0);
    }
    player.weapon_model_index=0;player.sequence=3;player.gait_sequence=0;
    CHECK_FALSE(present(4,1.4).attachment_model_index);
    player.weapon_model_index=71;player.sequence=0;player.frame=0;
    CHECK(present(5,1.5,true).attachment_model_index==71);
    player.weapon_model_index=99;CHECK(present(6,1.6).attachment_model_index==99);
    player.weapon_model_index.reset();CHECK_FALSE(present(7,1.7).attachment_model_index);
    CHECK(host.sample(1.7).primary_fire_starts==0);CHECK(host.sample(1.7).recoil_starts==0);
}

TEST_CASE("E24 players timed grenades Hornet RPG custom Egon beam and sprite share atomic ordinary packet framing",
          "[e24][grenade-world][packet-entities][transactional][no-stock]") {
    namespace d=hlclient::test::delta_fixture;namespace pf=hlclient::test::player_origin_fixture;
    const d::Field ordinary[]{
        {"origin[2]",0x80000004U,8,18,32000,4000},{"angles[0]",0x10U,12,16},
        {"modelindex",8,24,10},{"skin",0x80000002U,36,9},{"movetype",8,68,8}};
    const d::Field custom[]{
        {"rendermode",8,32,8},{"origin[0]",0x80000004U,0,17,32000,4000},
        {"origin[1]",0x80000004U,4,17,32000,4000},{"origin[2]",0x80000004U,8,17,32000,4000},
        {"angles[0]",0x80000004U,12,17,32000,4000},{"angles[1]",0x80000004U,16,17,32000,4000},
        {"angles[2]",0x80000004U,20,17,32000,4000},{"sequence",8,28,16},{"skin",8,36,16},
        {"modelindex",8,24,16},{"scale",4,40,8,4000,400},{"body",8,44,8},
        {"rendercolor.r",1,48,8},{"rendercolor.g",1,49,8},{"rendercolor.b",1,50,8},
        {"renderfx",8,52,8},{"renderamt",8,56,8},{"frame",4,60,8},{"animtime",4,64,8,4000,400}};
    g::DeltaSchemaRegistryBuilder registry;
    for(const auto& bytes:std::array{d::schema("entity_state_player_t",ordinary),d::schema("entity_state_t",ordinary),
            d::schema("custom_entity_state_t",custom)}) {
        const auto parsed=g::DeltaDescriptionParser{}.parse(bytes,0);REQUIRE(parsed);REQUIRE(registry.insert(*parsed.schema));
    }
    const auto schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(registry).publish());
    const auto baselines=pf::baselines(*schemas);
    for(const unsigned grenade_slot:{74U,237U})for(const unsigned count:{2U,16U}) {
        d::BitWriter w;w.write(7,8);w.write(std::bit_cast<std::uint32_t>(30.F),32);w.write(40,8);w.write(count+6,16);
        const auto header=[&](bool beam,bool preceding) {
            w.write(1,1);w.write(beam ? 1U : 0U,1);w.write(preceding ? 1U : 0U,1);if(preceding)w.write(1,6);
        };
        const auto fields=[&](double z,unsigned model,unsigned move,bool inherit_z=false) {
            w.write(1,3);w.write(inherit_z ? 30U : 31U,8);if(!inherit_z)pf::coordinate(w,z,8,18);
            w.write(16384,16);w.write(model,10);w.write(0,9);w.write(move,8);
        };
        header(false,false);fields(-12,7,3);header(false,false);fields(-16,8,3);
        header(true,true);w.write(3,3);w.write(0xf7,8);w.write(0xff,8);w.write(7,8);w.write(16,8);
        for(const double value:{100.,20.,-100.,-200.,-300.})pf::coordinate(w,value,8,17);
        w.write(0,16);w.write(4097,16);w.write(319,16);w.write(40,8);w.write(20,8);
        w.write(50,8);w.write(50,8);w.write(125,8);w.write(0,8);w.write(255,8);w.write(0,8);w.write(50,8);
        for(unsigned i=0;i<count;++i){header(false,true);fields(-16.0-i,grenade_slot,10,i==0);}
        header(false,true);fields(-70,82,5); // ordinary Hornet, not BOUNCE or beam schema
        header(false,true);fields(-90,93,5); // ordinary RPG resource, independent slot
        header(false,true);fields(-120,319,0);w.write(0,16);w.align_zero();
        for(const bool malformed:{false,true}) {
            c::ClientWorldState world;g::RuntimeReplayInitialization init;
            init.generation=1;init.max_clients=2;init.schemas=schemas;init.baselines=baselines;
            const auto made=g::RuntimeReplaySession::initialize(init,world);REQUIRE(made);
            const auto before=world.runtime_observation();g::RuntimeReplayRecord record;
            record.generation=1;record.record_identity=record.record_ordinal=1;record.payload=d::owning_payload(w.bytes());
            record.payload.source_sequence=20;if(malformed)record.payload.bytes.push_back(std::byte{254});
            record.initial_cursor=*g::StockRuntimeSourceCursor::create(0,0,record.payload.bytes.size());
            const auto applied=made.session->apply_record(record);INFO((applied.error ? applied.error->context : "ready"));
            if(malformed){CHECK_FALSE(applied);CHECK(world.runtime_observation()==before);continue;}
            REQUIRE(applied);const auto current=world.runtime_observation();REQUIRE(current->packet_entities.size()==count+6);
            CHECK(current->packet_entities[0].player_movement_schema);CHECK(current->packet_entities[1].player_movement_schema);
            const auto& beam=current->packet_entities[2];REQUIRE(beam.custom_beam);CHECK_FALSE(beam.ordinary_visual_schema);
            CHECK(beam.origin.z==-16);CHECK(beam.angles.x==-100);CHECK(beam.custom_beam->sprite_model_index==319);
            for(unsigned i=0;i<count;++i) {
                const auto& grenade=current->packet_entities[i+3];CHECK(grenade.ordinary_visual_schema);
                CHECK_FALSE(grenade.custom_beam);CHECK_FALSE(grenade.player_movement_schema);
                CHECK(grenade.origin.z==-16.0-i);CHECK(grenade.angles.x==90);CHECK(grenade.model_index==grenade_slot);
                CHECK(grenade.brush_move_type==10);
            }
            CHECK(current->packet_entities[count+3].model_index==82);CHECK(current->packet_entities[count+3].brush_move_type==5);
            CHECK(current->packet_entities[count+4].model_index==93);CHECK(current->packet_entities[count+4].brush_move_type==5);
            CHECK(current->packet_entities[count+5].model_index==319);CHECK(current->packet_entities[count+5].brush_move_type==0);
            CHECK_FALSE(made.session->apply_record(record));CHECK(world.runtime_observation()==current);
        }
    }
}
