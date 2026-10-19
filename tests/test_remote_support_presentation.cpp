#include "vertical_lift_test_fixture.hpp"
#include <hlclient/app/remote_support_presentation.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace {
namespace client=hlclient::client;
namespace goldsrc=hlclient::goldsrc;
namespace gc=hlclient::goldsrc::collision;
namespace app=hlclient::app;

std::shared_ptr<const hlclient::collision::CollisionWorldPackage> thin_ceiling() {
  using namespace hlclient::collision;
  using hlclient::tests::collision_brush_fixture::contents;
  const auto source=hlclient::tests::vertical_lift::package();
  auto planes=std::vector<CollisionPlane>{source->planes().begin(),source->planes().end()};
  auto nodes=std::vector<CollisionNode>{source->nodes().begin(),source->nodes().end()};
  auto clips=std::vector<CollisionClipnode>{source->clipnodes().begin(),source->clipnodes().end()};
  auto models=std::vector<CollisionModel>{source->models().begin(),source->models().end()};
  const auto split=[&](double distance,int front,int back) {
    const auto plane=static_cast<std::uint32_t>(planes.size());
    planes.push_back({{0,0,1},distance,plane,2});
    const auto root=static_cast<std::uint32_t>(nodes.size());
    const auto node=[](int i) {return i<0 ? CollisionNodeChild{CollisionNodeChildKind::leaf,i==-1 ? 1U : 0U} :
        CollisionNodeChild{CollisionNodeChildKind::node,static_cast<std::uint32_t>(i)};};
    const auto clip=[](int i) {return i<0 ? CollisionClipnodeChild{CollisionClipnodeChildKind::terminal,0U,contents(i)} :
        CollisionClipnodeChild{CollisionClipnodeChildKind::clipnode,static_cast<std::uint32_t>(i),contents(-1)};};
    nodes.push_back({plane,{node(front),node(back)}});clips.push_back({plane,{clip(front),clip(back)}});
    return root;
  };
  for(unsigned hull=0;hull<4;++hull) {
    const double height=hull==0 ? 0 : hull==1 ? 36 : hull==2 ? 32 : 18;
    const auto lower=split(128.0-height,-2,static_cast<int>(models[0].hulls[hull].root.index));
    models[0].hulls[hull].root.index=split(132.0+height,-1,static_cast<int>(lower));
  }
  return std::make_shared<const CollisionWorldPackage>(std::move(planes),std::move(nodes),
      std::vector<CollisionLeaf>{source->leaves().begin(),source->leaves().end()},
      std::move(clips),std::move(models),source->identity());
}

struct Fixture {
  std::shared_ptr<const gc::BrushCollisionModelLibrary> library;
  std::array<std::shared_ptr<client::RuntimeClientObservationState>,3> states;
  std::array<goldsrc::ReferenceBrushCollisionContext,3> contexts;
  std::shared_ptr<const gc::BrushCollisionScene> rendered;
  goldsrc::RuntimePlayerInterpolationAnchor anchor;
  client::RuntimePacketEntityObservation sampled;
  hlclient::collision::CollisionQueryScratch scratch;
  Fixture(double speed=100.0, unsigned duck=0U, bool ceiling=false,
      std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package={}) {
    const auto built=gc::build_brush_collision_model_library(
        package ? std::move(package) : hlclient::tests::vertical_lift::package(ceiling));
    REQUIRE(built);library=built.library;
    for(std::size_t i=0;i<3U;++i) {
      const double t=1.0+.1*static_cast<double>(i),z=40.0+(t-1.0)*speed;
      states[i]=std::make_shared<client::RuntimeClientObservationState>();
      auto& state=*states[i];state.generation=1;state.publication_revision=i+1U;
      state.entity_metadata={1U,client::RuntimeObservationFreshness::observed_in_record,
          client::RuntimeObservationCompleteness::complete_reconstruction,
          client::RuntimeObservationSource{i+1U,i+1U}};
      state.server_time_seconds=t;state.server_time_metadata=state.entity_metadata;
      client::RuntimePacketEntityObservation actor;
      actor.entity_number=2;actor.player_movement_schema=true;actor.model_index=7;
      actor.origin={-20.0+20.0*static_cast<double>(i),0.0,z+(duck ? 18.0 : 36.0)+.03125};
      actor.angles={0.0,0.0,0.0};actor.move_type=3;actor.use_hull=duck;actor.spectator=false;
      actor.weapon_model_index=22;actor.gait_sequence=1;actor.effects=0;
      state.packet_entities.push_back(actor);
      contexts[i]=hlclient::tests::vertical_lift::context(library,t,z,i+1U);
      REQUIRE(contexts[i].scene);
    }
    anchor={2U,states[0],states[1],1.05,false,{}};
    sampled=states[0]->packet_entities[0];sampled.origin.x=-10.0;
    sampled.origin.z=40.0+.05*speed+(duck ? 18.0 : 36.0)+.03125;
    rendered=goldsrc::ReferenceVerticalSupportMotion{contexts[1],contexts[2]}.sample(1.25);
    REQUIRE(rendered);
  }
  auto run() {
    return app::sample_remote_vertical_support(anchor,sampled,*states[2],contexts,rendered,scratch);
  }
};
}

TEST_CASE("Remote lift rider is expressed at the exact rendered support height",
    "[remote-support][movement][brush][no-stock]") {
  for(const double speed:{100.0,-100.0,0.0})for(const unsigned duck:{0U,1U}) {
    CAPTURE(speed,duck);Fixture f{speed,duck};
    const auto before=*f.states[0]->packet_entities[0].origin.z;
    const auto committed=f.contexts.back().scene->instances().front().transform.translation.z;
    const auto result=f.run();REQUIRE(result);
    CHECK(result->origin.x==Catch::Approx(-10.0));CHECK(result->origin.y==0.0F);
    CHECK(result->origin.z==Catch::Approx(40.0+.25*speed+(duck ? 18.0 : 36.0)+.03125).margin(.0001));
    CHECK(result->offset_z==Catch::Approx(.20*speed).margin(.0001));
    CHECK(result->support.source_entity_index==42U);CHECK(result->support.source_model_index==1U);
    CHECK(result->sample_seconds==1.05);
    CHECK(*f.states[0]->packet_entities[0].origin.z==before);
    CHECK(f.contexts.back().scene->instances().front().transform.translation.z==committed);
    // Original delayed position really was below/above the moving support.
    CHECK(result->origin.z-*f.sampled.origin.z==Catch::Approx(.20*speed).margin(.0001));
  }
}

TEST_CASE("Remote support offset follows stopping and reversal without extrapolating the actor",
    "[remote-support][movement][brush][no-stock]") {
  for(const double endpoint:{50.0,45.0}) {
    Fixture f;
    f.contexts[2]=hlclient::tests::vertical_lift::context(f.library,1.2,endpoint,3);
    f.rendered=f.contexts[2].scene;
    const auto result=f.run();REQUIRE(result);
    CHECK(result->origin.z==Catch::Approx(endpoint+36.03125));
    CHECK(result->origin.x==Catch::Approx(-10.0));
  }
}

TEST_CASE("Remote support never magnets jumping falling flying dead or unobserved actors",
    "[remote-support][movement][brush][no-stock]") {
  Fixture f;
  SECTION("jumping endpoint") {f.states[1]->packet_entities[0].origin.z=*f.states[1]->packet_entities[0].origin.z+5.0;}
  SECTION("falling above platform") {
    for(auto& state:f.states)state->packet_entities[0].origin.z=*state->packet_entities[0].origin.z+1.0;
    f.sampled.origin.z=*f.sampled.origin.z+1.0;
  }
  SECTION("runs off edge between raw endpoints") {f.states[1]->packet_entities[0].origin.x=100.0;}
  SECTION("sample outside support") {f.sampled.origin.x=100.0;}
  SECTION("player did not ride rising support") {f.states[1]->packet_entities[0].origin.z=f.states[0]->packet_entities[0].origin.z;}
  SECTION("ladder/fly") {f.sampled.move_type=5U;}
  SECTION("death") {f.sampled.weapon_model_index=0U;}
  SECTION("spectator") {f.sampled.spectator=true;}
  SECTION("missing movement schema") {f.sampled.player_movement_schema=false;}
  SECTION("missing usehull") {f.sampled.use_hull.reset();}
  SECTION("unavailable movement mode") {f.sampled.move_type.reset();}
  SECTION("unavailable spectator metadata") {f.sampled.spectator.reset();}
  SECTION("hull transition") {f.states[1]->packet_entities[0].use_hull=1U;}
  SECTION("model transition") {f.states[1]->packet_entities[0].model_index=8U;}
  SECTION("life/teleport boundary") {f.anchor.discontinuity=true;}
  SECTION("no independent bracket") {f.anchor.previous=f.anchor.current;}
  SECTION("sample outside bracket") {f.anchor.sample_seconds=1.15;}
  SECTION("sample does not belong to bracket") {f.sampled.origin.z=*f.sampled.origin.z+1.0;}
  CHECK_FALSE(f.run());
}

TEST_CASE("Remote support requires continuous same-library vertical PUSH identity",
    "[remote-support][movement][brush][no-stock]") {
  Fixture f;
  SECTION("pusher removed") {f.contexts[2].pushers.clear();}
  SECTION("missing intermediate context") {f.contexts[1].scene.reset();}
  SECTION("generation boundary") {f.contexts[2].generation=2;}
  SECTION("latest publication differs") {f.states[2]->publication_revision=4;}
  SECTION("latest scene absent") {f.contexts[2].scene.reset();}
  SECTION("time missing") {f.contexts[2].server_time.reset();}
  SECTION("nonfinite time") {f.contexts[2].server_time=std::numeric_limits<double>::quiet_NaN();}
  SECTION("time goes backwards") {f.contexts[2].server_time=1.05;}
  SECTION("source gap") {f.contexts[2].server_time=1.7;}
  SECTION("same-time teleport") {f.contexts[2].server_time=1.1;}
  SECTION("support overspeed") {f.contexts[2]=hlclient::tests::vertical_lift::context(f.library,1.2,100.0,3);}
  SECTION("different model library") {
    const auto other=gc::build_brush_collision_model_library(hlclient::tests::vertical_lift::package());REQUIRE(other);
    f.rendered=hlclient::tests::vertical_lift::context(other.library,1.25,65.0,4).scene;
  }
  SECTION("unbounded final extrapolation") {f.rendered=hlclient::tests::vertical_lift::context(f.library,2.0,150.0,4).scene;}
  SECTION("different rendered entity") {
    auto instances=std::vector<gc::BrushCollisionSceneInstance>{f.rendered->instances().begin(),f.rendered->instances().end()};
    instances.front().identity.source_entity_index=43U;
    f.rendered=std::make_shared<const gc::BrushCollisionScene>(f.library,std::move(instances),f.rendered->role_provider_profile());
  }
  SECTION("horizontal movement") {
    auto instances=std::vector<gc::BrushCollisionSceneInstance>{f.rendered->instances().begin(),f.rendered->instances().end()};
    instances.front().transform.translation.x=1.0F;
    f.rendered=std::make_shared<const gc::BrushCollisionScene>(f.library,std::move(instances),f.rendered->role_provider_profile());
  }
  SECTION("rotation") {
    auto instances=std::vector<gc::BrushCollisionSceneInstance>{f.rendered->instances().begin(),f.rendered->instances().end()};
    instances.front().transform.rotation_degrees.y=1.0F;
    f.rendered=std::make_shared<const gc::BrushCollisionScene>(f.library,std::move(instances),f.rendered->role_provider_profile());
  }
  CHECK_FALSE(f.run());
}

TEST_CASE("Remote support correction cannot place a rider through a ceiling",
    "[remote-support][movement][brush][no-stock]") {
  Fixture f{100.0,0U,true};
  f.rendered=hlclient::tests::vertical_lift::context(f.library,1.3,100.0,4).scene;
  CHECK_FALSE(f.run()); // Standing player top 172 crosses the fixture ceiling160.
}

TEST_CASE("Remote support carry rejects a thin fixed ceiling even when final hull is clear",
    "[remote-support][movement][brush][no-stock]") {
  Fixture f{100.0,0U,false,thin_ceiling()};
  f.contexts[2]=hlclient::tests::vertical_lift::context(f.library,1.2,75.5,3);
  f.rendered=hlclient::tests::vertical_lift::context(f.library,1.45,139.5,4).scene;
  gc::BrushCollisionSceneTraceRequest endpoint;
  endpoint.start=endpoint.end={-10.0F,0.0F,175.53125F};
  endpoint.hull=hlclient::collision::CollisionHullOrdinal::standing_32x32x72;
  const auto clear=gc::BrushCollisionSceneQuery{f.rendered}.trace_hull(endpoint,f.scratch);
  REQUIRE(clear);CHECK_FALSE(clear.result->trace.start_solid);
  // Both raw anchors lie below the four-unit slab, final actor wholly above.
  // A final-only occupancy test would incorrectly accept this visual carry.
  CHECK_FALSE(f.run());
}

TEST_CASE("Remote support has an explicit bounded history admission",
    "[remote-support][movement][brush][no-stock]") {
  Fixture f;
  const std::vector<goldsrc::ReferenceBrushCollisionContext> oversized(18U,f.contexts[0]);
  CHECK_FALSE(app::sample_remote_vertical_support(f.anchor,f.sampled,*f.states[2],oversized,f.rendered,f.scratch));
  CHECK_FALSE(app::sample_remote_vertical_support(f.anchor,f.sampled,*f.states[2],{},f.rendered,f.scratch));
}
