#pragma once
#include "collision_brush_test_fixture.hpp"
#include <hlclient/goldsrc/reference_brush_collision.hpp>

namespace hlclient::tests::lift_top_e32_1 {
// Independently authored point/standing/large/duck Minkowski hulls. The finite
// lift rises to z70 beside an upper world floor, not an endless rising plane.
// Upper floor starts at x64 (or x-64 for the mirrored/backward control).
inline std::shared_ptr<const collision::CollisionWorldPackage> package(
    const bool mirrored = false, const bool wall = false, const bool low_ceiling = false) {
  using namespace collision;
  using namespace collision_brush_fixture;
  std::vector<CollisionPlane> planes;
  std::vector<CollisionNode> nodes;
  std::vector<CollisionClipnode> clips;
  const auto split = [&](assets::AssetVector3 normal, double distance, int front, int back) {
    const auto plane = static_cast<std::uint32_t>(planes.size());
    planes.push_back({normal, distance, plane, 3});
    const auto index = static_cast<std::uint32_t>(nodes.size());
    const auto node_child = [](int value) { return value < 0
        ? CollisionNodeChild{CollisionNodeChildKind::leaf, value == -1 ? 1U : 0U}
        : CollisionNodeChild{CollisionNodeChildKind::node, static_cast<std::uint32_t>(value)}; };
    const auto clip_child = [](int value) { return value < 0
        ? CollisionClipnodeChild{CollisionClipnodeChildKind::terminal, 0U, contents(value)}
        : CollisionClipnodeChild{CollisionClipnodeChildKind::clipnode, static_cast<std::uint32_t>(value), contents(-1)}; };
    nodes.push_back({plane, {node_child(front), node_child(back)}});
    clips.push_back({plane, {clip_child(front), clip_child(back)}});
    return static_cast<int>(index);
  };
  auto world = model(0U, 0U, 0U), lift = model(1U, 0U, 0U);
  world.source_bounds = {{-4096, -4096, -4096}, {4096, 4096, 4096}};
  lift.source_bounds = {{-64, -64, -8}, {64, 64, 0}};
  for (unsigned hull = 0U; hull < 4U; ++hull) {
    const float radius = hull == 0U ? 0.F : hull == 2U ? 32.F : 16.F;
    const float height = hull == 0U ? 0.F : hull == 1U ? 36.F : hull == 2U ? 32.F : 18.F;
    auto root = split({0, 0, 1}, -128.F + height, -1, -2);
    const auto upper = split({0, 0, 1}, 70.F + height, -1, -2);
    root = split({mirrored ? -1.F : 1.F, 0, 0}, 64.F - radius, upper, root);
    if (wall) root = split({mirrored ? -1.F : 1.F, 0, 0}, 160.F - radius, -2, root);
    if (low_ceiling) root = split({0, 0, 1}, 144.F - height, -2, root);
    world.hulls[hull].root.index = static_cast<std::uint32_t>(root);
    root = split({0, 0, 1}, -8.F - height, -2, -1);
    root = split({0, 0, 1}, height, -1, root);
    root = split({0, 1, 0}, -64.F - radius, root, -1);
    root = split({0, 1, 0}, 64.F + radius, -1, root);
    root = split({1, 0, 0}, -64.F - radius, root, -1);
    root = split({1, 0, 0}, 64.F + radius, -1, root);
    lift.hulls[hull].root.index = static_cast<std::uint32_t>(root);
  }
  return std::make_shared<const CollisionWorldPackage>(planes, nodes,
      std::vector<CollisionLeaf>{{0U, contents(-2)}, {1U, contents(-1)}}, clips,
      std::vector<CollisionModel>{world, lift},
      CollisionWorldIdentity{assets::AssetSourceFingerprint{0x45333231U, 0x544f5031U}, 1U});
}
} // namespace hlclient::tests::lift_top_e32_1
