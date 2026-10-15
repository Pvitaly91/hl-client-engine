#include "tripmine_world_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include <catch2/catch_approx.hpp>
namespace {namespace t=hlclient::tests;namespace f=t::tripmine_world_fixture;
namespace wf=t::hand_grenade_world_fixture;namespace api=hlclient::game_api;namespace g=hlclient::goldsrc;
namespace r=hlclient::renderer;using Catch::Approx;}
TEST_CASE("E26 server v_tripmine world body orientation and delayed ENTPOINT beam require actual publications","[e26][tripmine-world][no-stock]") {
    f::Owned o;o.publish(2,1,10);auto out=o.scene(10);
    REQUIRE(o.world.entity_frame());REQUIRE(o.world.entity_frame()->studio_instances().size()==1);
    CHECK(o.world.entity_frame()->studio_instances()[0].body_value==3);
    CHECK(o.assets->studio_model_data(75)->sequences[7].label=="world");
    CHECK(o.state.packet_entities[0].sequence==7);
    CHECK_FALSE(out.follow_trail_entities);CHECK(o.assets->summary().model_names.at(75)=="models/v_tripmine.mdl");
    REQUIRE(o.assets->present_entities(o.module.host,14,1,o.world,{320,240}));CHECK_FALSE(o.scene(14).follow_trail_entities);
    o.state.packet_entities[0].solid=2;o.state.packet_entities[0].angles={0.,90.,0.};
    o.state.packet_entities.push_back(f::Owned::beam());o.publish(3,3.5,14.1);out=o.scene(14.1);
    REQUIRE(out.follow_trail_entities);REQUIRE(out.follow_trail_entities->frame->sprite_instances().size()==1);
    const auto& beam=out.follow_trail_entities->frame->sprite_instances()[0];
    CHECK(beam.ribbon_width==Approx(1));CHECK(beam.opacity==Approx(64/255.F));CHECK(beam.tint[1]==Approx(214/255.F));
    REQUIRE(beam.ribbon_end);CHECK(beam.ribbon_end->x==Approx(0));
    CHECK(o.module.host.drain_temporary_effects(14.1).count==0);
}
TEST_CASE("E26 multiple mines beam endpoints owner death respawn and server removal are independent","[e26][tripmine-world][lifecycle]") {
    f::Owned o;auto other=f::Owned::mine(41);other.owner_entity_reference=2;other.origin={8.,0.,0.};
    o.state.packet_entities.push_back(other);o.state.packet_entities.push_back(f::Owned::beam());
    o.state.packet_entities.push_back(f::Owned::beam(51,41));o.publish(2,4,10);
    REQUIRE(o.scene().follow_trail_entities);CHECK(o.scene().follow_trail_entities->frame->sprite_instances().size()==2);
    auto dead=o.module.observed;dead.lifecycle.state=hlclient::client::LocalPlayerLifeState::dead;
    dead.lifecycle.life_epoch=2;dead.receiving_client->health=0;o.module.host.observe(dead,.1);
    REQUIRE(o.assets->present_entities(o.module.host,11,1,o.world,{320,240}));
    CHECK(o.world.entity_frame()->studio_instances().size()==2);CHECK(o.scene(11).follow_trail_entities->frame->sprite_instances().size()==2);
    o.state.packet_entities.erase(o.state.packet_entities.begin());o.publish(3,4.1,11.1);
    auto scene=o.scene(11.1);REQUIRE(scene.follow_trail_entities);CHECK(scene.follow_trail_entities->frame->sprite_instances().size()==1);
    o.state.packet_entities.clear();o.publish(4,4.2,11.2);CHECK_FALSE(o.scene(11.2).follow_trail_entities);
    CHECK(o.module.host.drain_temporary_effects(11.2).count==0);CHECK(o.module.host.drain_remote_audio().count==0);
}
TEST_CASE("E26 an old server beam cannot reattach to a reused removed parent slot","[e26][tripmine-world][slot-reuse]") {
    f::Owned o;o.state.packet_entities.push_back(f::Owned::beam());o.publish(2,4,10);REQUIRE(o.scene().follow_trail_entities);
    o.state.packet_entities.erase(o.state.packet_entities.begin());o.publish(3,4.1,10.1);CHECK_FALSE(o.scene(10.1).follow_trail_entities);
    auto unrelated=f::Owned::mine();unrelated.model_index=312;unrelated.sequence=0;unrelated.body=0;unrelated.origin={32.,0.,16.};
    o.state.packet_entities.insert(o.state.packet_entities.begin(),unrelated);o.publish(4,4.2,10.2);
    CHECK_FALSE(o.scene(10.2).follow_trail_entities);
    o.state.packet_entities.erase(o.state.packet_entities.begin()+1);o.publish(5,4.3,10.3);
    o.state.packet_entities.push_back(f::Owned::beam());o.publish(6,4.4,10.4);
    REQUIRE(o.assets->present_entities(o.module.host,10.6,1,o.world,{320,240}));CHECK(o.scene(10.6).follow_trail_entities);
}
TEST_CASE("E26 beam break removal and delayed explosion or smoke are server evidence not local triggers","[e26][tripmine-world][temporary-effects]") {
    f::Owned o;o.state.packet_entities.push_back(f::Owned::beam());o.publish(2,4,10);
    CHECK(o.module.host.drain_temporary_effects(10).count==0);
    o.state.packet_entities.pop_back();o.publish(3,4.1,10.1);CHECK_FALSE(o.scene(10.1).follow_trail_entities);
    CHECK(o.module.host.drain_temporary_effects(10.1).count==0);
    const auto explosion=wf::effect(10,.2);o.module.host.committed_temporary_effect(explosion);
    o.module.host.committed_temporary_effect(explosion);const auto batch=o.module.host.drain_temporary_effects(.2);REQUIRE(batch.count==1);
    hlclient::app::TemporaryEffectPresentation effects;effects.reset(1);effects.consume(batch,.2);
    r::RenderScene scene;effects.present(scene,.2);CHECK(effects.sprites().size()==1);
    o.module.host.committed_temporary_effect(wf::effect(11,.4,true));effects.consume(o.module.host.drain_temporary_effects(.4),.4);
    effects.present(scene,.4);CHECK(effects.sprites().size()==2);effects.present(scene,6);CHECK(effects.sprites().empty());
}
TEST_CASE("E26 server mine deploy charge activate spatial PCM and pitch have no duplicate local cue","[e26][tripmine-world][audio]") {
    struct Samples:g::SoundAssets {wf::Samples data;
        g::SoundAssetResult request(std::uint16_t slot) noexcept override{return data.request(slot);}
        std::string_view virtual_name(std::uint16_t slot) const noexcept override {
            return slot==51 ? "sound/weapons/mine_deploy.wav" : slot==52 ? "sound/weapons/mine_charge.wav" : "sound/weapons/mine_activate.wav";
        }} samples;
    wf::Sink sink;g::ServerAudio audio(sink);audio.present({},{{},{0,-1,0},1,false},{},1);
    for(unsigned slot:{51U,52U,53U}) {auto e=wf::sound(slot,40,slot,-20,slot==52 ? 4U : 2U);
        e.sound.volume=255;e.sound.pitch=static_cast<std::uint8_t>(slot==53 ? 75U : 100U);audio.consume(e,{});audio.consume(e,{});audio.update(&samples,{});}
    CHECK(sink.starts.size()==3);CHECK(sink.starts.back().pitch==Approx(.75));
    std::array<float,4096> pcm{};sink.output.mixer.render(pcm);
    CHECK(std::ranges::all_of(pcm,[](float v){return std::isfinite(v);}));
    CHECK(std::ranges::any_of(pcm,[](float v){return std::abs(v)>.0001F;}));
    f::Owned o;o.publish(2,10,10);REQUIRE(o.assets->present_entities(o.module.host,14,1,o.world,{320,240}));
    CHECK(o.module.host.drain_remote_audio().count==0);CHECK(o.module.host.drain_audio().count==0);
}
TEST_CASE("E26 owned body3 world model and low-opacity server laser draw through normal OpenGL","[e26][tripmine-world][opengl]") {
    auto gl=t::entity_opengl_fixture::try_context();if(!gl)SKIP("OpenGL 3.3 Core unavailable");
    REQUIRE(t::entity_opengl_fixture::capable_context());gl->initialize_renderer();f::Owned o;
    o.publish(2,3.5,10);
    const auto draw=[&](r::RenderScene s){s.clear_color={0,0,0,1};s.camera={{0,0,40},{0,0,16},{0,1,0},1.047F,.1F,128};
        gl->renderer().render(s,{96,96});CHECK(glGetError()==GL_NO_ERROR);return t::entity_opengl_fixture::framebuffer();};
    const auto baseline=draw({});const auto mine=draw(o.scene());const bool mine_visible=mine!=baseline;CHECK(mine_visible);
    o.state.packet_entities.push_back(f::Owned::beam());o.publish(3,4,10.1);const auto laser=draw(o.scene(10.1));const bool laser_visible=laser!=mine;CHECK(laser_visible);
    o.state.packet_entities.clear();o.publish(4,4.1,10.2);const bool removed=draw(o.scene(10.2))==baseline;CHECK(removed);
}
TEST_CASE("E26 normal asset composition point query reads fresh canonical solids and excludes receiving actor","[e26][tripmine-world][placement-query]") {
    f::Owned o;o.state.packet_entities[0].solid=2;o.publish(2,2,10);
    api::PlacementTraceRequest request{{-16,0,16},{16,0,16},1,1,2U,3U};
    const auto hit=o.assets->placement_trace(request);REQUIRE(hit.valid);REQUIRE(hit.hit_entity);CHECK(*hit.hit_entity==40);
    CHECK(hit.fraction<1);CHECK_FALSE(hit.start_solid);
    auto latest=std::make_shared<hlclient::client::RuntimeClientObservationState>(o.state);
    latest->packet_entities[0].solid=0;wf::stamp(*latest,3,3);o.assets->placement_observation(latest);
    const auto gone=o.assets->placement_trace(request);REQUIRE(gone.valid);CHECK(gone.fraction==1);CHECK_FALSE(gone.hit_entity);
    latest=std::make_shared<hlclient::client::RuntimeClientObservationState>(o.state);wf::stamp(*latest,4,4);
    latest->packet_entities[0].entity_number=1;o.assets->placement_observation(latest);
    const auto own=o.assets->placement_trace(request);REQUIRE(own.valid);CHECK_FALSE(own.hit_entity);
    request.generation=2;CHECK_FALSE(o.assets->placement_trace(request).valid);
}
TEST_CASE("E26 remote p_tripmine merges onto committed moving crouched player without local placement effects","[e26][tripmine-world][remote-pmodel]") {
    f::Owned o;hlclient::client::RuntimePacketEntityObservation p;p.entity_number=2;p.model_index=7;
    p.weapon_model_index=71;p.ordinary_visual_schema=p.player_movement_schema=true;p.origin={0.,0.,16.};p.angles={0.,90.,0.};
    p.sequence=0;p.gait_sequence=0;p.body=0;p.skin=0;p.frame=0;p.frame_rate=1;p.animation_time_seconds=1;
    p.render_effect=0;p.render_amount=255;p.effects=0;p.solid=3;p.use_hull=0;p.controllers.fill(127);p.blending.fill(127);
    o.state.packet_entities.insert(o.state.packet_entities.begin(),p);o.publish(2,2,10);
    const auto instances=o.world.entity_frame()->studio_instances();
    const auto child=std::ranges::find_if(instances,[](const auto& e){return e.entity_number==2 && e.subinstance_ordinal==1;});
    REQUIRE(child!=instances.end());CHECK(o.assets->summary().model_names.at(71)=="models/p_tripmine.mdl");
    const auto before=child->transform.origin;o.state.packet_entities[0].origin.x=8;o.state.packet_entities[0].sequence=2;
    o.state.packet_entities[0].gait_sequence=2;o.state.packet_entities[0].use_hull=1;o.publish(3,2.1,10.1);
    REQUIRE(o.assets->present_entities(o.module.host,10.3,1,o.world,{320,240}));
    const auto next=o.world.entity_frame()->studio_instances();
    const auto moved=std::ranges::find_if(next,[](const auto& e){return e.entity_number==2 && e.subinstance_ordinal==1;});
    REQUIRE(moved!=next.end());CHECK(moved->transform.origin.x>before.x);
    CHECK(o.module.host.drain_remote_effects(10.3).count==0);CHECK(o.module.host.drain_temporary_effects(10.3).count==0);
}
