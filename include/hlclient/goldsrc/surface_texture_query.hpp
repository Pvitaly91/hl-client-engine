#pragma once

#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>

#include <memory>
#include <optional>
#include <string>

namespace hlclient::goldsrc {

// Presentation-only texture trace. Collision establishes the supporting model;
// this query searches only that model's imported source faces. It never uses a
// collision-plane ordinal as a render-face ordinal or changes movement.
struct SurfaceTextureResult {
  enum class Status { found, unmapped_face, invalid_support, geometry_unavailable,
      ambiguous_support, source_texture_unavailable };
  Status status{Status::geometry_unavailable};
  std::string texture_name;
  std::optional<std::uint32_t> source_surface_index, source_material_index,
      source_texture_index, source_model_index;
  std::optional<assets::AssetVector3> world_origin;
};

class SurfaceTextureQuery final {
public:
  explicit SurfaceTextureQuery(
      std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> scene,
      std::shared_ptr<const hlclient::collision::CollisionWorldPackage> collision = {})
      : scene_(std::move(scene)), world_query_(std::move(collision)) {}

  [[nodiscard]] SurfaceTextureResult at_support(
      const hlclient::movement::LocalPlayerMovementState& state,
      const collision::BrushCollisionScene* brushes) const noexcept;

private:
  std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> scene_;
  hlclient::collision::CollisionWorldQuery world_query_;
  // Session-thread only. Bounded reusable query scratch, not movement state.
  // A missing point-hull package fails closed for static-world material lookup.
  mutable hlclient::collision::CollisionQueryScratch scratch_;
};

} // namespace hlclient::goldsrc
