#include "satchel_test_fixture.hpp"
#include "hand_grenade_world_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "delta_test_fixture.hpp"
#include "player_origin_test_fixture.hpp"
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <catch2/catch_approx.hpp>
namespace {namespace t=hlclient::tests;namespace f=t::hand_grenade_world_fixture;namespace sf=t::satchel_fixture;
namespace c=hlclient::client;namespace api=hlclient::game_api;namespace g=hlclient::goldsrc;
std::unique_ptr<hlclient::app::RuntimeReplayLocalAssets> assets(t::ScopedLocalResourceTestRoot& root) {
    (void)f::owned(root);
    auto bytes=t::literal_minimal_goldsrc_studio_v10();t::studio_write_fixed_string(bytes,t::kSyntheticStudioSequenceOffset,32,"owned_satchel");
    root.write("valve","models/w_satchel.mdl",bytes);
    const auto resources=t::readiness_fixture::parse_resource_list({{2,"maps/test.bsp",9,0,0},
        {2,"models/w_satchel.mdl",237,0,0},{2,"models/grenade.mdl",312,0,0},
        {2,"sprites/owned_explosion.spr",319,0,0},{2,"sprites/owned_smoke.spr",320,0,0}});
    auto made=hlclient::app::RuntimeReplayLocalAssets::create(resources,t::readiness_fixture::parse_server_info("maps/test.bsp"),
        root.path(),"valve",hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(made.projection);return std::move(made.projection);
}
}
TEST_CASE("E25 authoritative satchels persist thirty seconds and owner death removes only server selected charge","[e25][satchel-world][no-stock]") {
    t::ScopedLocalResourceTestRoot root;auto a=assets(root);auto s=f::state();s.packet_entities[0].owner_entity_reference=1;
    auto other=s.packet_entities[0];other.entity_number=41;other.owner_entity_reference=2;other.origin={20,0,0};s.packet_entities.push_back(other);
    f::stamp(s,1,1);
    c::ClientWorldState world;REQUIRE(a->reset_generation(s,world));REQUIRE(a->studio_model_data(237));
    CHECK(a->studio_model_data(237)->sequences[0].label=="owned_satchel");
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});host.set_movement_audio_time_origin(0);
    const auto present=[&](double at){REQUIRE(a->present_entities(host,at,1,world,{320,240}));};
    const auto before=s;present(99);present(129);CHECK(world.entity_frame()->studio_instances().size()==2);CHECK(s==before);
    auto dead=s;dead.lifecycle.state=c::LocalPlayerLifeState::dead;dead.lifecycle.life_epoch=2;dead.receiving_client->health=0;
    host.observe(dead,30);present(130);CHECK(world.entity_frame()->studio_instances().size()==2);
    s.packet_entities.erase(s.packet_entities.begin());f::stamp(s,2,31);f::project(*a,s,world);present(131);
    REQUIRE(world.entity_frame()->studio_instances().size()==1);CHECK(world.entity_frame()->studio_instances()[0].entity_number==41);
    CHECK(host.drain_temporary_effects(31).count==0);CHECK(host.drain_remote_audio().count==0);CHECK(host.drain_remote_effects(31).count==0);
    s.packet_entities.clear();f::stamp(s,3,32);f::project(*a,s,world);present(132);CHECK(world.entity_frame()->studio_instances().empty());
    s=f::state(312);s.packet_entities[0].origin={300,0,0};f::stamp(s,4,33);f::project(*a,s,world);present(133);
    CHECK(world.entity_frame()->studio_instances().size()==1);CHECK(a->studio_model_data(312)->sequences[0].label=="owned_contact");
}
TEST_CASE("E25 three distinct server explosions survive one detonation window and exact duplicates dedup","[e25][satchel-world][temporary-effects][no-stock]") {
    sf::Module m(0,1);m.host.submit(sf::command(1,.02,g::kReferenceGoldSrcButtonAttack2),.02);
    CHECK(m.host.drain_temporary_effects(.02).count==0);
    for(unsigned i=0;i<3;++i) {auto event=f::effect(i+1,.1);event.origin={static_cast<float>(i*30),0,0};
        event.record=event.record_ordinal=42;event.entry_ordinal=i;
        event.message_bit_offset=i*256;m.host.committed_temporary_effect(event);m.host.committed_temporary_effect(event);}
    const auto batch=m.host.drain_temporary_effects(.1);CHECK(batch.count==3);
    hlclient::app::TemporaryEffectPresentation effects;effects.reset(1);effects.consume(batch,.1);
    hlclient::renderer::RenderScene scene;effects.present(scene,.1);
    CHECK(effects.sprites().size()==3);CHECK(m.host.drain_temporary_effects(.1).count==0);
    for(unsigned i=0;i<3;++i) {auto smoke=f::effect(i+11,.4,true);smoke.origin={static_cast<float>(i*30),0,0};
        smoke.record=smoke.record_ordinal=43;smoke.entry_ordinal=i;
        smoke.message_bit_offset=i*256;m.host.committed_temporary_effect(smoke);}
    const auto smoke=m.host.drain_temporary_effects(.4);CHECK(smoke.count==3);effects.consume(smoke,.4);
    effects.present(scene,5);CHECK(effects.sprites().empty());
}
TEST_CASE("E25 server g-bounce sounds stay spatial separate and never infer collisions","[e25][satchel-world][audio][no-stock]") {
    struct Samples:g::SoundAssets {
        f::Samples storage;
        g::SoundAssetResult request(std::uint16_t slot) noexcept override { return storage.request(slot); }
        std::string_view virtual_name(std::uint16_t slot) const noexcept override {
            return slot==51 ? "sound/weapons/g_bounce1.wav" : slot==52 ? "sound/weapons/g_bounce2.wav" : "sound/weapons/g_bounce3.wav";
        }
    };
    // Use a standalone owning provider: all positive samples belong to one server event.
    Samples resources;f::Sink sink;g::ServerAudio audio(sink);audio.present({},{{},{0,-1,0},1,false},{},1);
    for(unsigned i=0;i<3;++i) {auto e=f::sound(i+1,40+i,51+i,-50-static_cast<float>(i*25));audio.consume(e,{});audio.consume(e,{});}
    audio.update(&resources,{});CHECK(sink.starts.size()==3);std::array<float,1024> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::ranges::any_of(pcm,[](float v){return std::isfinite(v) && std::abs(v)>.001F;}));
    CHECK(sink.starts[0].origin.y==-50);CHECK(sink.starts[1].origin.y==-75);CHECK(sink.starts[2].origin.y==-100);
}
TEST_CASE("E25 BOUNCE satchel uses bounded authoritative arc slide and rest interpolation","[e25][satchel-world][interpolation][no-stock]") {
    t::ScopedLocalResourceTestRoot root;auto a=assets(root);auto s=f::state();s.packet_entities[0].origin={0,0,20};
    f::stamp(s,1,1);
    c::ClientWorldState world;REQUIRE(a->reset_generation(s,world));sf::Module m;
    REQUIRE(a->present_entities(m.host,99,1,world,{320,240}));
    s.packet_entities[0].origin={20,0,0};s.packet_entities[0].angles={30,10,20};f::stamp(s,2,1.1);f::project(*a,s,world);
    REQUIRE(a->present_entities(m.host,99.1,1,world,{320,240}));REQUIRE(a->present_entities(m.host,99.15,1,world,{320,240}));
    const auto& halfway=world.entity_frame()->studio_instances()[0].transform;
    CHECK(halfway.origin.x==Catch::Approx(10).margin(.002));CHECK(halfway.origin.z==Catch::Approx(10).margin(.002));
    s.packet_entities[0].origin={10,0,10};f::stamp(s,3,1.2);f::project(*a,s,world);
    REQUIRE(a->present_entities(m.host,99.2,1,world,{320,240}));REQUIRE(a->present_entities(m.host,99.25,1,world,{320,240}));
    CHECK(world.entity_frame()->studio_instances()[0].transform.origin.x==Catch::Approx(15).margin(.002));
    CHECK(world.entity_frame()->studio_instances()[0].transform.origin.z==Catch::Approx(5).margin(.002));
    REQUIRE(a->present_entities(m.host,130,1,world,{320,240}));CHECK(world.entity_frame()->studio_instances()[0].transform.origin.z==10);
    CHECK(m.host.drain_audio().count==0);CHECK(m.host.drain_temporary_effects(31).count==0);
}
TEST_CASE("E25 owned server model and three explosion sprites draw independently and clean up GPU state","[e25][satchel-world][opengl][no-stock]") {
    namespace r=hlclient::renderer;
    auto gl=t::entity_opengl_fixture::try_context();if(!gl)SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());gl->initialize_renderer();
    t::ScopedLocalResourceTestRoot root;auto a=assets(root);auto s=f::state();c::ClientWorldState world;REQUIRE(a->reset_generation(s,world));
    const auto draw=[&](r::RenderScene scene) {scene.clear_color={0,0,0,1};scene.camera={{0,0,20},{0,0,0},{0,1,0},1.04719755F,.1F,128};
        gl->renderer().render(scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);return t::entity_opengl_fixture::framebuffer();};
    const auto baseline=draw({});r::RenderScene scene;scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
    const bool model_visible=draw(scene)!=baseline;CHECK(model_visible);
    sf::Module m(0,1);hlclient::app::TemporaryEffectPresentation effects;effects.reset(1);
    for(unsigned i=0;i<3;++i) {auto event=f::effect(i+1,0);event.record=event.record_ordinal=50;event.entry_ordinal=i;event.message_bit_offset=i*256;
        event.origin={static_cast<float>(int(i)-1)*6,0,0};event.scale_tenths=5;m.host.committed_temporary_effect(event);}
    const auto batch=m.host.drain_temporary_effects(0);REQUIRE(batch.count==3);effects.consume(batch,0);scene={};effects.present(scene,0);
    CHECK(effects.sprites().size()==3);scene.transient_sprite_entities=a->materialize_temporary_sprites(effects.sprites(),0);
    REQUIRE(scene.transient_sprite_entities);CHECK(scene.transient_sprite_entities->frame->sprite_instances().size()==3);
    const auto full=draw(scene);const bool explosions_visible=full!=baseline;CHECK(explosions_visible);
    for(std::size_t i=0;i<3;++i) {
        std::vector samples(effects.sprites().begin(),effects.sprites().end());samples.erase(samples.begin()+static_cast<std::ptrdiff_t>(i));
        auto reduced=scene;reduced.transient_sprite_entities=a->materialize_temporary_sprites(samples,0);
        const bool contribution=draw(reduced)!=full;CHECK(contribution);
    }
    const auto uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;
    const auto repeated=draw(scene);const bool stable=draw(scene)==repeated;CHECK(stable);
    CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==uploads);
    scene={};effects.present(scene,5);scene.transient_sprite_entities=a->materialize_temporary_sprites(effects.sprites(),5);
    CHECK_FALSE(scene.transient_sprite_entities);const bool restored=draw(scene)==baseline;CHECK(restored);
    GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);CHECK(depth==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
    gl->release_renderer();
}
TEST_CASE("E25 mixed player Satchel Hornet rocket custom beam and sprite packets retain independent owner schemas","[e25][satchel-world][packet-entities][no-stock]") {
    namespace d=hlclient::test::delta_fixture;namespace pf=hlclient::test::player_origin_fixture;
    const d::Field ordinary[]{{"origin[2]",0x80000004U,8,18,32000,4000},{"angles[0]",0x10U,12,16},
        {"modelindex",8,24,10},{"skin",0x80000002U,36,9},{"movetype",8,68,8},{"owner",8,72,5}};
    const d::Field custom[]{{"rendermode",8,32,8},{"origin[0]",0x80000004U,0,17,32000,4000},
        {"origin[1]",0x80000004U,4,17,32000,4000},{"origin[2]",0x80000004U,8,17,32000,4000},
        {"angles[0]",0x80000004U,12,17,32000,4000},{"angles[1]",0x80000004U,16,17,32000,4000},
        {"angles[2]",0x80000004U,20,17,32000,4000},{"sequence",8,28,16},{"skin",8,36,16},
        {"modelindex",8,24,16},{"scale",4,40,8,4000,400},{"body",8,44,8},
        {"rendercolor.r",1,48,8},{"rendercolor.g",1,49,8},{"rendercolor.b",1,50,8},
        {"renderfx",8,52,8},{"renderamt",8,56,8},{"frame",4,60,8},{"animtime",4,64,8,4000,400}};
    g::DeltaSchemaRegistryBuilder registry;
    for(const auto& bytes:std::array{d::schema("entity_state_player_t",ordinary),d::schema("entity_state_t",ordinary),d::schema("custom_entity_state_t",custom)}) {
        const auto parsed=g::DeltaDescriptionParser{}.parse(bytes,0);REQUIRE(parsed);REQUIRE(registry.insert(*parsed.schema));
    }
    const auto schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(registry).publish());const auto baselines=pf::baselines(*schemas);
    for(const unsigned count:{2U,16U})for(const unsigned slot:{74U,237U}) {
        d::BitWriter w;w.write(7,8);w.write(std::bit_cast<std::uint32_t>(30.F),32);w.write(40,8);w.write(count+6,16);
        const auto header=[&](bool beam,bool preceding){w.write(1,1);w.write(beam ? 1U : 0U,1);w.write(preceding ? 1U : 0U,1);if(preceding)w.write(1,6);};
        const auto fields=[&](double z,unsigned model,unsigned move,unsigned owner,bool inherited=false) {
            w.write(1,3);w.write(inherited ? 62U : 63U,8);if(!inherited)pf::coordinate(w,z,8,18);
            w.write(16384,16);w.write(model,10);w.write(0,9);w.write(move,8);w.write(owner,5);
        };
        header(false,false);fields(-12,7,3,0);header(false,false);fields(-16,8,3,0);
        header(true,true);w.write(3,3);w.write(0xf7,8);w.write(0xff,8);w.write(7,8);w.write(16,8);
        for(const double value:{100.,20.,-100.,-200.,-300.})pf::coordinate(w,value,8,17);
        w.write(0,16);w.write(4097,16);w.write(319,16);w.write(40,8);w.write(20,8);
        w.write(50,8);w.write(50,8);w.write(125,8);w.write(0,8);w.write(255,8);w.write(0,8);w.write(50,8);
        for(unsigned i=0;i<count;++i){header(false,true);fields(-16.0-i,slot,10,i%2+1,i==0);}
        header(false,true);fields(-70,82,5,1);header(false,true);fields(-90,93,5,2);header(false,true);fields(-120,319,0,0);
        w.write(0,16);w.align_zero();
        for(const bool malformed:{false,true}) {
            c::ClientWorldState world;g::RuntimeReplayInitialization init;init.generation=1;init.max_clients=2;init.schemas=schemas;init.baselines=baselines;
            const auto made=g::RuntimeReplaySession::initialize(init,world);REQUIRE(made);const auto before=world.runtime_observation();
            g::RuntimeReplayRecord record;record.generation=1;record.record_identity=record.record_ordinal=1;record.payload=d::owning_payload(w.bytes());
            record.payload.source_sequence=20;if(malformed)record.payload.bytes.push_back(std::byte{254});
            record.initial_cursor=*g::StockRuntimeSourceCursor::create(0,0,record.payload.bytes.size());const auto applied=made.session->apply_record(record);
            if(malformed){CHECK_FALSE(applied);CHECK(world.runtime_observation()==before);continue;}
            REQUIRE(applied);const auto current=world.runtime_observation();REQUIRE(current->packet_entities.size()==count+6);
            CHECK(current->packet_entities[0].player_movement_schema);CHECK(current->packet_entities[1].player_movement_schema);
            REQUIRE(current->packet_entities[2].custom_beam);CHECK_FALSE(current->packet_entities[2].ordinary_visual_schema);
            for(unsigned i=0;i<count;++i) {
                const auto& charge=current->packet_entities[i+3];CHECK(charge.ordinary_visual_schema);CHECK_FALSE(charge.custom_beam);
                CHECK(charge.origin.z==-16.0-i);CHECK(charge.model_index==slot);CHECK(charge.brush_move_type==10);CHECK(charge.owner_entity_reference==i%2+1);
            }
            CHECK(current->packet_entities[count+3].model_index==82);CHECK(current->packet_entities[count+4].model_index==93);CHECK(current->packet_entities[count+5].model_index==319);
            CHECK_FALSE(made.session->apply_record(record));CHECK(world.runtime_observation()==current);
        }
    }
}
