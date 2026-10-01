#pragma once
#include "collision_brush_test_fixture.hpp"
#include <hlclient/goldsrc/reference_brush_collision.hpp>

namespace hlclient::tests::vertical_lift {
inline goldsrc::ReferenceBrushCollisionContext context(
    std::shared_ptr<const goldsrc::collision::BrushCollisionModelLibrary> library,
    double time, double z, std::uint64_t record) {
  client::RuntimeClientObservationState o;
  o.generation=1; o.publication_revision=record;
  o.entity_metadata={1,client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,client::RuntimeObservationSource{record}};
  o.server_time_seconds=time; o.server_time_metadata=o.entity_metadata;
  client::RuntimePacketEntityObservation e;
  e.entity_number=42; e.model_index=27; e.solid=4; e.brush_move_type=7;
  e.origin={0.,0.,z}; e.angles={0.,0.,0.}; o.packet_entities.push_back(e);
  return goldsrc::build_reference_brush_collision(std::move(library),{{27,"*1"}},o,1);
}
// Project-owned finite 128x128x8 platform. Independently authored Minkowski
// hulls: point, standing, large, duck. World floor -128, optional ceiling 160
// and stationary ledge x>=80,z<=0. No game assets or test movement engine.
inline std::shared_ptr<const collision::CollisionWorldPackage> package(
    bool ceiling = false, bool ledge = false) {
  using namespace collision;
  using namespace collision_brush_fixture;
  std::vector<CollisionPlane> planes;
  std::vector<CollisionNode> nodes;
  std::vector<CollisionClipnode> clips;
  auto split = [&](assets::AssetVector3 normal, double distance, int front, int back) {
    const auto plane = static_cast<std::uint32_t>(planes.size());
    planes.push_back({normal,distance,plane,3});
    const auto index = static_cast<std::uint32_t>(nodes.size());
    const auto node_child = [](int value) { return value < 0
      ? CollisionNodeChild{CollisionNodeChildKind::leaf, value == -1 ? 1U : 0U}
      : CollisionNodeChild{CollisionNodeChildKind::node,static_cast<std::uint32_t>(value)}; };
    const auto clip_child = [](int value) { return value < 0
      ? CollisionClipnodeChild{CollisionClipnodeChildKind::terminal,0U,contents(value)}
      : CollisionClipnodeChild{CollisionClipnodeChildKind::clipnode,static_cast<std::uint32_t>(value),contents(-1)}; };
    nodes.push_back({plane,{node_child(front),node_child(back)}});
    clips.push_back({plane,{clip_child(front),clip_child(back)}});
    return static_cast<int>(index);
  };
  auto world = model(0,0,0), brush = model(1,0,0);
  world.source_bounds = {{-4096,-4096,-4096},{4096,4096,4096}};
  brush.source_bounds = {{-64,-64,-8},{64,64,0}};
  for (unsigned hull=0; hull<4; ++hull) {
    const float radius = hull == 0 ? 0.F : hull == 2 ? 32.F : 16.F;
    const float height = hull == 0 ? 0.F : hull == 1 ? 36.F : hull == 2 ? 32.F : 18.F;
    int root = split({0,0,1},-128+height,-1,-2);
    if (ceiling) root = split({0,0,1},160-height,-2,root);
    if (ledge) { const auto top=split({0,0,1},height,root,-2); root=split({1,0,0},80-radius,top,root); }
    world.hulls[hull].root.index = static_cast<std::uint32_t>(root);
    root = split({0,0,1},-8-height,-2,-1);
    root = split({0,0,1},height,-1,root);
    root = split({0,1,0},-64-radius,root,-1);
    root = split({0,1,0},64+radius,-1,root);
    root = split({1,0,0},-64-radius,root,-1);
    root = split({1,0,0},64+radius,-1,root);
    brush.hulls[hull].root.index = static_cast<std::uint32_t>(root);
  }
  return std::make_shared<const CollisionWorldPackage>(planes,nodes,
      std::vector<CollisionLeaf>{{0,contents(-2)},{1,contents(-1)}},clips,
      std::vector<CollisionModel>{world,brush},
      CollisionWorldIdentity{assets::AssetSourceFingerprint{0x44344c49U,0x46540001U},30U});
}
}
