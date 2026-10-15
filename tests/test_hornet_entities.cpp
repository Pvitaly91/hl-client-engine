#include "rpg_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "delta_test_fixture.hpp"
#include "player_origin_test_fixture.hpp"
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>

namespace {
namespace t=hlclient::tests;
namespace c=hlclient::client;
namespace app=hlclient::app;
namespace r=hlclient::renderer;
namespace api=hlclient::game_api;
namespace readiness=t::readiness_fixture;
using Catch::Approx;

void stamp(c::RuntimeClientObservationState& s,unsigned ordinal,double seconds) {
    s.publication_revision=ordinal;s.server_time_seconds=seconds;
    s.entity_metadata=s.server_time_metadata=s.client_metadata={1,c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,c::RuntimeObservationSource{ordinal,ordinal,1,0,8}};
}
c::RuntimeClientObservationState state(unsigned ordinal=1,unsigned slot=237) {
    auto s=t::rpg_fixture::state();
    stamp(s,ordinal,1.0+(ordinal-1)*.1);
    c::RuntimePacketEntityObservation e;e.entity_number=40;e.model_index=slot;
    e.origin={0.0,0.0,0.0};e.angles={0.0,0.0,0.0};e.ordinary_visual_schema=true;
    e.brush_move_type=5;e.sequence=0;e.frame=0;e.effects=0;
    s.packet_entities={e};s.canonical_state_hash=c::runtime_observation_canonical_hash(s);return s;
}
c::CommittedTemporaryEffect follow(unsigned record=1,unsigned entity=40,unsigned sprite=319,bool orange=false) {
    c::CommittedTemporaryEffect e;e.generation=1;e.record=e.record_ordinal=record;e.received_at_seconds=0;
    e.kind=c::TemporaryEffectKind::beam_follow;e.entity_attachment=static_cast<std::uint16_t>(entity);
    e.sprite_index=static_cast<std::uint16_t>(sprite);e.life_tenths=10;e.width_tenths=2;
    e.color=orange ? std::array<std::uint8_t,3>{255,128,0} : std::array<std::uint8_t,3>{179,39,14};
    e.brightness=128;return e;
}
std::unique_ptr<app::RuntimeReplayLocalAssets> owned(t::ScopedLocalResourceTestRoot& root,
    unsigned slot=237,unsigned sprite=319,bool model=true,bool trail=true) {
    t::SyntheticBspBuilder bsp;const std::string text="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
    const auto eb=std::as_bytes(std::span{text});bsp.lump(t::SyntheticBspLumpId::entities).assign(eb.begin(),eb.end());
    t::SyntheticWad3Entry texture;texture.name="TEST_QUAD";texture.payload=t::synthetic_goldsrc_miptex("TEST_QUAD",64,64);
    root.write("valve","test.wad",t::synthetic_wad3({texture}).bytes);root.write("valve","maps/test.bsp",bsp.build());
    if(model)root.write("valve","models/hornet.mdl",t::literal_minimal_goldsrc_studio_v10());
    if(trail) {
        auto bytes=t::sprite_fixture::literal_single_sprite(1);
        // White owned material isolates exact server tint, independent of a
        // shipped sprite's artwork. No installed game assets are required.
        std::fill(bytes.begin()+42,bytes.begin()+42+768,std::byte{255});
        root.write("valve","sprites/laserbeam.spr",bytes);
    }
    const auto resources=readiness::parse_resource_list({{2,"maps/test.bsp",9,0,0},
        {2,"models/hornet.mdl",static_cast<std::uint16_t>(slot),0,0},
        {2,"sprites/laserbeam.spr",static_cast<std::uint16_t>(sprite),0,0}});
    auto result=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test.bsp"),
        root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
    REQUIRE(result.projection);return std::move(result.projection);
}
void project(app::RuntimeReplayLocalAssets& assets,c::RuntimeClientObservationState& s,c::ClientWorldState& world) {
    s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
    const auto result=assets.project_entities(s,world);INFO((result.error ? result.error->context : "ready"));REQUIRE(result);
}
std::size_t changed(const std::vector<std::byte>& a,const std::vector<std::byte>& b) {
    REQUIRE(a.size()==b.size());std::size_t n=0;
    for(std::size_t i=0;i<a.size();i+=4)if(a[i]!=b[i]||a[i+1]!=b[i+1]||a[i+2]!=b[i+2])++n;
    return n;
}
}

TEST_CASE("E23 generic fast follow ribbon ends at current presented entity between history samples",
          "[e23][hornet-entities][temporary-effects][no-stock]") {
    app::EntityFollowPresentation p;auto s=state();p.observe(s);p.consume(follow(),0);
    r::RenderScene scene;p.present(s.packet_entities,scene,0);
    s=state(2);s.packet_entities[0].origin.x=120;p.observe(s);p.present(s.packet_entities,scene,.1);
    REQUIRE(p.segments().size()==1);CHECK(p.segments().back().end.x==120);
    const auto samples=p.statistics().sampled;
    // At 144 Hz a 1,200 unit/s server path can advance >8 units before the
    // next 20-ms retained sample. The live tip must not lag behind its model.
    s.packet_entities[0].origin.x=128.4;p.present(s.packet_entities,scene,.107);
    REQUIRE_FALSE(p.segments().empty());CHECK(p.segments().back().end.x==Approx(128.4));
    CHECK(p.statistics().sampled==samples);
    const auto count=p.segments().size();p.present(s.packet_entities,scene,.107);
    REQUIRE(p.segments().size()==count);CHECK(p.segments().back().end.x==Approx(128.4));
    CHECK(p.statistics().sampled==samples);
    s.packet_entities[0].effects=128;p.present(s.packet_entities,scene,.11);
    REQUIRE(p.segments().size()==1);CHECK(p.segments().back().end.x==120); // no invented hidden head
}

TEST_CASE("E23 two server colored narrow trails curve independently and decay through death model zero and reuse",
          "[e23][hornet-entities][temporary-effects][lifecycle][no-stock]") {
    app::EntityFollowPresentation p;auto s=state();auto second=s.packet_entities[0];second.entity_number=41;
    second.origin.y=20;s.packet_entities.push_back(second);p.observe(s);
    p.consume(follow(),0);p.consume(follow(2,41,27,true),0);p.consume(follow(2,41,27,true),0);
    r::RenderScene scene;p.present(s.packet_entities,scene,0);
    s.entity_metadata.source->record_ordinal=2;s.packet_entities[0].origin.x=10;s.packet_entities[1].origin.x=30;
    p.observe(s);p.present(s.packet_entities,scene,.1);
    s.entity_metadata.source->record_ordinal=3;s.lifecycle.state=c::LocalPlayerLifeState::dead;
    s.packet_entities[0].origin.y=10;s.packet_entities[1].origin.x=60;
    p.observe(s);scene={};p.present(s.packet_entities,scene,.2);
    REQUIRE(p.segments().size()==4);CHECK(p.statistics().accepted==2);CHECK(p.statistics().duplicate==1);
    CHECK(p.segments()[0].end.x==10);CHECK(p.segments()[0].end.y==0);
    CHECK(p.segments()[1].start.x==10);CHECK(p.segments()[1].end.y==10);
    CHECK(p.segments()[3].end.x==60);CHECK(p.segments()[3].end.y==20);
    for(const auto& segment:p.segments()) {
        CHECK_FALSE(segment.puff);CHECK(segment.width==Approx(.2));CHECK(segment.opacity>0);
        CHECK(segment.opacity<=128.0F/255);const bool orange=segment.sprite_slot==27;
        CHECK(segment.color[0]==Approx(orange ? 1.0 : 179.0/255));
        CHECK(segment.color[1]==Approx((orange ? 128.0 : 39.0)/255));
        CHECK(segment.color[2]==Approx((orange ? 0.0 : 14.0)/255));
    }
    CHECK(scene.world_flash_count==0);CHECK(scene.world_effect_light_count==0);CHECK(p.heads().empty());
    const auto old=p.lifetime(40);s.entity_metadata.source->record_ordinal=4;s.packet_entities[0].model_index=0;
    p.observe(s);CHECK(p.lifetime(40)==0);CHECK(p.lifetime(41)!=0);p.present(s.packet_entities,scene,.3);
    CHECK(p.segments().size()==4);CHECK(p.statistics().detached==1);
    s.entity_metadata.source->record_ordinal=5;s.packet_entities.erase(s.packet_entities.begin());p.observe(s);
    p.present(s.packet_entities,scene,.4);CHECK(p.segments().size()==4);
    auto reused=state(6).packet_entities[0];reused.origin={200.0,200.0,0.0};
    s.entity_metadata.source->record_ordinal=6;s.packet_entities.insert(s.packet_entities.begin(),reused);p.observe(s);
    CHECK(p.lifetime(40)!=old);p.present(s.packet_entities,scene,.5);
    for(const auto& segment:p.segments())CHECK(segment.end.x<=60); // never reattach old path
    s.entity_metadata.source->record_ordinal=7;s.packet_entities.clear();p.observe(s);p.present({},scene,1.21);
    CHECK(p.segments().empty());p.reset(2);CHECK(p.lifetime(41)==0);CHECK(p.segments().empty());
}

TEST_CASE("E23 normal host samples server FLY curves angles and fast darts without weapon or target inference",
          "[e23][hornet-entities][composition][entity-interpolation][no-stock]") {
    for(const unsigned slot:{74U,237U}) {
        t::ScopedLocalResourceTestRoot root;auto assets=owned(root,slot);t::rpg_fixture::Module module;
        c::ClientWorldState world;auto first=state(1,slot);first.packet_entities[0].angles={10.0,350.0,0.0};
        const auto present=[&](double seconds) {
            const auto result=assets->present_entities(module.host,seconds,1,world,{320,240});
            INFO((result.error ? result.error->context : "ready"));REQUIRE(result);
        };
        auto dart=first.packet_entities[0];dart.entity_number=41;dart.origin.y=100;first.packet_entities.push_back(dart);
        first.canonical_state_hash=c::runtime_observation_canonical_hash(first);REQUIRE(assets->reset_generation(first,world));
        present(99);
        REQUIRE(world.entity_frame()->studio_instances().size()==2);
        auto second=first;stamp(second,2,1.1);
        second.packet_entities[0].origin={40.0,20.0,0.0};second.packet_entities[0].angles={30.0,10.0,0.0};
        second.packet_entities[1].origin.x=120;project(*assets,second,world);const auto immutable=second;
        present(99.1);present(99.15);
        const auto instances=world.entity_frame()->studio_instances();REQUIRE(instances.size()==2);
        CHECK(instances[0].transform.origin.x==Approx(20).margin(.002));
        CHECK(instances[0].transform.origin.y==Approx(10).margin(.002));
        CHECK(instances[0].transform.rotation_degrees.y==Approx(-20).margin(.002));
        CHECK(std::remainder(instances[0].transform.rotation_degrees.z,360.0F)==Approx(0).margin(.002));
        CHECK(instances[1].transform.origin.x==Approx(60).margin(.002));CHECK(instances[1].transform.origin.y==100);
        CHECK(second==immutable);
        auto third=second;stamp(third,3,1.2);
        third.packet_entities[0].origin={40.0,60.0,0.0};third.packet_entities[0].angles={40.0,90.0,0.0};
        third.packet_entities[1].origin.x=240;project(*assets,third,world);
        present(99.2);present(99.25);
        CHECK(world.entity_frame()->studio_instances()[0].transform.origin.x==Approx(40).margin(.002));
        CHECK(world.entity_frame()->studio_instances()[0].transform.origin.y==Approx(40).margin(.002));
        CHECK(world.entity_frame()->studio_instances()[1].transform.origin.x==Approx(180).margin(.002));
        module.observed.receiving_client->health=0;module.observed.publication_revision=2;module.host.observe(module.observed,.3);
        present(99.27);CHECK(world.entity_frame()->studio_instances().size()==2);
        auto hidden=third;stamp(hidden,4,1.3);
        hidden.packet_entities[0].model_index=0;project(*assets,hidden,world);
        present(99.28);
        REQUIRE(world.entity_frame()->studio_instances().size()==1);CHECK(world.entity_frame()->studio_instances()[0].entity_number==41);
        stamp(hidden,5,1.4);
        project(*assets,hidden,world);present(99.3);
        CHECK(world.entity_frame()->studio_instances().size()==1); // zero-model entity remains alive, never stale fallback
        auto removed=hidden;stamp(removed,6,1.5);
        removed.packet_entities.clear();project(*assets,removed,world);
        present(99.4);CHECK(world.entity_frame()->studio_instances().empty());
        auto reused=state(7,slot);reused.packet_entities[0].origin={300.0,300.0,10.0};project(*assets,reused,world);
        present(99.5);
        REQUIRE(world.entity_frame()->studio_instances().size()==1);CHECK(world.entity_frame()->studio_instances()[0].transform.origin.x==300);
        CHECK(module.host.drain_temporary_effects(.5).count==0);CHECK(module.host.drain_audio().count==0);
    }
}

TEST_CASE("E23 production Studio and curved narrow follow pixels use independent manifest resources and expire",
          "[e23][hornet-entities][opengl][runtime-replay][no-stock]") {
    auto context=t::entity_opengl_fixture::try_context();if(!context)SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());context->initialize_renderer();auto& renderer=context->renderer();
    const auto pixels=[&](r::RenderScene scene) {
        scene.clear_color={0,0,0,1};scene.camera={{0,0,20},{0,0,0},{0,1,0},1.04719755F,.1F,128};
        renderer.render(scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);return t::entity_opengl_fixture::framebuffer();
    };
    const auto baseline=pixels({});
    for(const bool orange:{false,true}) {
        t::ScopedLocalResourceTestRoot root;auto assets=owned(root);auto s=state();c::ClientWorldState world;
        REQUIRE(assets->reset_generation(s,world));r::RenderScene model_scene;
        model_scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
        const auto model=pixels(model_scene);CHECK(changed(model,baseline)>4);
        const auto uploaded=renderer.entity_statistics().studio_asset_upload_count;
        s.packet_entities[0].origin.x=3;s.packet_entities[0].angles.y=90;s.entity_metadata.source->record_ordinal=2;project(*assets,s,world);
        model_scene.dynamic_entities=r::RenderDynamicEntities{world.entity_scene(),world.entity_frame(),{}};
        CHECK(changed(pixels(model_scene),model)>4);CHECK(renderer.entity_statistics().studio_asset_upload_count==uploaded);
        s.packet_entities[0].origin={-4.0,-3.0,0.0};s.entity_metadata.source->record_ordinal=3;project(*assets,s,world);
        assets->consume_follow(follow(1,40,319,orange),0);r::RenderScene trail;assets->present_entity_effects(trail,0);
        const std::array<hlclient::assets::AssetVector3,3> positions{{{4,-3,0},{4,3,0},{-2,3,0}}};
        for(std::size_t i=0;i<positions.size();++i) {
            const auto p=positions[i];s.packet_entities[0].origin={p.x,p.y,p.z};s.entity_metadata.source->record_ordinal=4+i;
            project(*assets,s,world);assets->present_entity_effects(trail,.1*(i+1));
        }
        REQUIRE(trail.follow_trail_entities);const auto strips=trail.follow_trail_entities->frame->sprite_instances();REQUIRE(strips.size()==3);
        CHECK(strips[0].transform.origin.x==-4);REQUIRE(strips[0].ribbon_end);CHECK(strips[0].ribbon_end->x==4);
        CHECK(strips[1].transform.origin.x==4);CHECK(strips[1].ribbon_end->y==3);
        for(const auto& strip:strips){REQUIRE(strip.ribbon_end);CHECK(strip.ribbon_width==Approx(.2));}
        const auto curved=pixels(trail);const auto count=changed(curved,baseline);CHECK(count>20);CHECK(count<500);
        unsigned peak_red{},peak_green{},peak_blue{};
        for(std::size_t i=0;i<curved.size();i+=4) {
            peak_red=std::max(peak_red,std::to_integer<unsigned>(curved[i]));
            peak_green=std::max(peak_green,std::to_integer<unsigned>(curved[i+1]));
            peak_blue=std::max(peak_blue,std::to_integer<unsigned>(curved[i+2]));
        }
        REQUIRE(peak_green>0);CHECK(double(peak_red)/peak_green==Approx(orange ? 255.0/128 : 179.0/39).margin(.4));
        CHECK(peak_blue<peak_green); // Actual GPU tint/brightness, not only submitted payload.
        // The historical U path must illuminate its lower horizontal arm, not
        // replace the path with a chord from the spawn to the current point.
        std::size_t lower_arm{};for(unsigned y=33;y<=37;++y)for(unsigned x=37;x<=59;++x) {
            const auto offset=(y*96+x)*4;
            if(curved[offset]!=baseline[offset])++lower_arm;
        }
        CHECK(lower_arm>5);
        CHECK(pixels(trail)==curved);
        s.packet_entities[0].model_index=0;s.entity_metadata.source->record_ordinal=8;project(*assets,s,world);
        REQUIRE(world.entity_frame()->studio_instances().empty());assets->present_entity_effects(trail,.4);
        CHECK(changed(pixels(trail),baseline)>0);
        s.packet_entities.clear();s.entity_metadata.source->record_ordinal=9;project(*assets,s,world);
        assets->present_entity_effects(trail,1.31);CHECK(pixels(trail)==baseline);
    }
    GLboolean depth{},writes{};glGetBooleanv(GL_DEPTH_TEST,&depth);glGetBooleanv(GL_DEPTH_WRITEMASK,&writes);
    CHECK(depth==GL_TRUE);CHECK(writes==GL_TRUE);CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
}

TEST_CASE("E23 missing projectile model and missing trail sprite degrade independently",
          "[e23][hornet-entities][runtime-replay][no-stock]") {
    for(const bool model_present:{false,true}) {
        t::ScopedLocalResourceTestRoot root;auto assets=owned(root,237,319,model_present,!model_present);
        auto s=state();c::ClientWorldState world;REQUIRE(assets->reset_generation(s,world));
        CHECK(world.entity_frame()->studio_instances().size()==(model_present ? 1U : 0U));
        assets->consume_follow(follow(),0);r::RenderScene scene;assets->present_entity_effects(scene,0);
        s.packet_entities[0].origin.x=10;s.entity_metadata.source->record_ordinal=2;project(*assets,s,world);
        assets->present_entity_effects(scene,.1);
        CHECK(bool(scene.follow_trail_entities)==!model_present);
        CHECK(world.entity_frame()->studio_instances().size()==(model_present ? 1U : 0U));
    }
}

TEST_CASE("E23 generic narrow follow crossing near plane does not form a fullscreen strip",
          "[e23][hornet-entities][opengl][near-camera][no-stock]") {
    auto context=t::entity_opengl_fixture::try_context();if(!context)SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());context->initialize_renderer();
    t::ScopedLocalResourceTestRoot root;auto assets=owned(root);auto s=state();c::ClientWorldState world;
    s.packet_entities[0].origin={0.0,0.0,19.95};s.canonical_state_hash=c::runtime_observation_canonical_hash(s);
    REQUIRE(assets->reset_generation(s,world));assets->consume_follow(follow(),0);
    r::RenderScene scene;assets->present_entity_effects(scene,0);
    s.packet_entities[0].origin={.5,0.0,19.5};s.entity_metadata.source->record_ordinal=2;project(*assets,s,world);
    assets->present_entity_effects(scene,.1);REQUIRE(scene.follow_trail_entities);
    scene.clear_color={0,0,0,1};scene.camera={{0,0,20},{0,0,0},{0,1,0},1.04719755F,.1F,128};
    auto blank=scene;blank.follow_trail_entities.reset();context->renderer().render(blank,{96,96});
    const auto baseline=t::entity_opengl_fixture::framebuffer();context->renderer().render(scene,{96,96});
    CHECK(glGetError()==GL_NO_ERROR);const auto n=changed(t::entity_opengl_fixture::framebuffer(),baseline);
    CHECK(n>0);CHECK(n<96U*96U/2U);
}

TEST_CASE("E23 mixed player multiple FLY projectile custom beam and sprite packet stays atomic",
          "[e23][hornet-entities][packet-entities][transactional][no-stock]") {
    namespace g=hlclient::goldsrc;namespace d=hlclient::test::delta_fixture;
    namespace pf=hlclient::test::player_origin_fixture;
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
    for(const unsigned count:{2U,8U,32U})for(const unsigned model_slot:{74U,237U}) {
        CAPTURE(count,model_slot);
        d::BitWriter w;w.write(7,8);w.write(std::bit_cast<std::uint32_t>(30.F),32);w.write(40,8);w.write(count+5,16);
        const auto header=[&](bool beam,bool preceding) {
            w.write(1,1);w.write(beam ? 1U : 0U,1);w.write(preceding ? 1U : 0U,1);if(preceding)w.write(1,6);
        };
        const auto fields=[&](double z,unsigned model,unsigned move,bool inherit_z=false) {
            w.write(1,3);w.write(inherit_z ? 30U : 31U,8);if(!inherit_z)pf::coordinate(w,z,8,18);
            w.write(16384,16);w.write(model,10);w.write(0,9);w.write(move,8);
        };
        header(false,false);fields(-12,7,3);
        header(false,false);fields(-16,8,3);
        // The beam deliberately inherits the preceding player's z through
        // compatible offsets while its angles use float, not ordinary angle.
        header(true,true);w.write(3,3);w.write(0xf7,8);w.write(0xff,8);w.write(7,8);w.write(16,8);
        for(const double value:{100.,20.,-100.,-200.,-300.})pf::coordinate(w,value,8,17);
        w.write(0,16);w.write(4097,16);w.write(319,16);w.write(40,8);w.write(20,8);
        w.write(50,8);w.write(50,8);w.write(125,8);w.write(0,8);w.write(255,8);w.write(0,8);w.write(50,8);
        for(unsigned i=0;i<count;++i) {header(false,true);fields(-16.0-i,model_slot,5,i==0);}
        header(false,true);fields(-90,312,10); // unrelated ordinary projectile
        header(false,true);fields(-120,319,0); // ordinary sprite model slot
        w.write(0,16);w.align_zero();
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
            REQUIRE(applied);const auto current=world.runtime_observation();REQUIRE(current->packet_entities.size()==count+5);
            CHECK(current->packet_entities[0].player_movement_schema);CHECK(current->packet_entities[1].player_movement_schema);
            const auto& beam=current->packet_entities[2];REQUIRE(beam.custom_beam);CHECK_FALSE(beam.ordinary_visual_schema);
            CHECK(beam.origin.z==-16);CHECK(beam.angles.x==-100);CHECK(beam.custom_beam->sprite_model_index==319);
            CHECK(beam.custom_beam->width_units==4);CHECK(beam.custom_beam->end_reference.entity==1);
            for(unsigned i=0;i<count;++i) {
                const auto& projectile=current->packet_entities[i+3];CHECK(projectile.entity_number==i+4);
                CHECK(projectile.ordinary_visual_schema);CHECK_FALSE(projectile.custom_beam);CHECK_FALSE(projectile.player_movement_schema);
                CHECK(projectile.origin.z==-16.0-i);CHECK(projectile.angles.x==90);CHECK(projectile.model_index==model_slot);
                CHECK(projectile.brush_move_type==5);CHECK(projectile.skin==0);
            }
            CHECK(current->packet_entities[count+3].model_index==312);CHECK(current->packet_entities[count+3].brush_move_type==10);
            CHECK(current->packet_entities[count+4].model_index==319);CHECK(current->packet_entities[count+4].brush_move_type==0);
            CHECK_FALSE(made.session->apply_record(record));CHECK(world.runtime_observation()==current);
        }
    }
}
