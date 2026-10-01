#include <hlclient/goldsrc/surface_texture_query.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_rigid_transform.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_math.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hlclient::goldsrc {
namespace {
using assets::AssetVector3;

[[nodiscard]] AssetVector3 subtract(AssetVector3 a, AssetVector3 b) noexcept {
  return {a.x-b.x, a.y-b.y, a.z-b.z};
}
[[nodiscard]] AssetVector3 cross(AssetVector3 a, AssetVector3 b) noexcept {
  return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
[[nodiscard]] double dot(AssetVector3 a, AssetVector3 b) noexcept {
  return static_cast<double>(a.x)*b.x+static_cast<double>(a.y)*b.y+
      static_cast<double>(a.z)*b.z;
}
[[nodiscard]] bool finite(AssetVector3 p) noexcept {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
[[nodiscard]] std::optional<double> intersect(AssetVector3 start,
    AssetVector3 end, AssetVector3 a, AssetVector3 b, AssetVector3 c) noexcept {
  const auto direction=subtract(end,start);
  const auto e1=subtract(b,a), e2=subtract(c,a);
  const auto normal=cross(e1,e2);
  // A face with its source normal pointing down cannot be the walkable top.
  if (normal.z <= 0.0F) return {};
  const auto p=cross(direction,e2);
  const double det=dot(e1,p);
  if (std::abs(det)<1e-8) return {};
  const double inverse=1.0/det;
  const auto tvec=subtract(start,a);
  const double u=dot(tvec,p)*inverse;
  if (u< -1e-5 || u>1.00001) return {};
  const auto q=cross(tvec,e1);
  const double v=dot(direction,q)*inverse;
  if (v< -1e-5 || u+v>1.00001) return {};
  const double t=dot(e2,q)*inverse;
  if (t<0.0 || t>1.0) return {};
  return t;
}

[[nodiscard]] SurfaceTextureResult query_package(
    const world_render::WorldRenderPackage& package,
    std::span<const std::uint32_t> selected_surfaces,
    AssetVector3 start, AssetVector3 end,
    std::optional<AssetVector3> support_normal={},
    std::optional<double> support_distance={}) {
  SurfaceTextureResult out;
  const auto& world=package.textured_world().world;
  const auto ranges=package.surface_ranges();
  const auto vertices=package.vertices();
  const auto indices=package.indices();
  const auto& materials=world.materials;
  double best=std::numeric_limits<double>::infinity();
  std::optional<std::uint32_t> best_surface;
  bool ambiguous=false;
  auto examine=[&](std::size_t ordinal) {
    if (ordinal>=ranges.size()) return;
    const auto& range=ranges[ordinal];
    if (range.source_world_surface_index>=world.surfaces.size() ||
        range.first_index>indices.size() ||
        range.index_count>indices.size()-range.first_index) return;
    const auto& bounds=range.bounds;
    if (std::max(start.x,end.x)<bounds.minimum.x-0.02F ||
        std::min(start.x,end.x)>bounds.maximum.x+0.02F ||
        std::max(start.y,end.y)<bounds.minimum.y-0.02F ||
        std::min(start.y,end.y)>bounds.maximum.y+0.02F ||
        std::max(start.z,end.z)<bounds.minimum.z-0.02F ||
        std::min(start.z,end.z)>bounds.maximum.z+0.02F) return;
    for (std::size_t i=range.first_index;i+2U<static_cast<std::size_t>(range.first_index)+range.index_count;i+=3U) {
      if (indices[i]>=vertices.size() || indices[i+1U]>=vertices.size() ||
          indices[i+2U]>=vertices.size()) continue;
      const auto a=vertices[indices[i]].position, b=vertices[indices[i+1U]].position,
          c=vertices[indices[i+2U]].position;
      auto normal=cross(subtract(b,a),subtract(c,a));
      const auto length=std::sqrt(dot(normal,normal));
      if(length<=0 || !std::isfinite(length)) continue;
      normal={static_cast<float>(normal.x/length),static_cast<float>(normal.y/length),
          static_cast<float>(normal.z/length)};
      // Source hull planes and render face ordinals are different namespaces.
      // Geometric agreement constrains this read-only trace to the real support,
      // not a parallel floor further below or a nearby incompatible slope.
      if(support_normal && dot(normal,*support_normal)<.9999) continue;
      if(support_distance && std::abs(dot(normal,a)-*support_distance)>.125) continue;
      const auto hit=intersect(start,end,a,b,c);
      if(!hit || *hit>best+1e-6) continue;
      if(std::abs(*hit-best)<=1e-6 && best_surface &&
          *best_surface!=range.source_world_surface_index) {ambiguous=true; continue;}
      if(*hit>=best) continue;
      ambiguous=false; best_surface=range.source_world_surface_index;
      best=*hit;
      const auto& surface=world.surfaces[range.source_world_surface_index];
      const auto material=surface.material_index;
      out.source_surface_index=surface.source_surface_ordinal;
      out.source_material_index=material;
      out.world_origin=AssetVector3{
          static_cast<float>(start.x+(end.x-start.x)*best),
          static_cast<float>(start.y+(end.y-start.y)*best),
          static_cast<float>(start.z+(end.z-start.z)*best)};
      if (material<materials.size() && materials[material].texture_name &&
          !materials[material].texture_name->empty()) {
        out.status=SurfaceTextureResult::Status::found;
        out.texture_name=*materials[material].texture_name;
        out.source_texture_index=materials[material].source_texture_index;
        const auto* binding=package.textured_world().textures.binding_for_material(material);
        if(!binding || (binding->status!=assets::WorldMaterialTextureBindingStatus::resolved_embedded &&
            binding->status!=assets::WorldMaterialTextureBindingStatus::resolved_wad3))
          out.status=SurfaceTextureResult::Status::source_texture_unavailable;
      } else {
        out.status=SurfaceTextureResult::Status::unmapped_face;
        out.texture_name.clear();
      }
    }
  };
  if (selected_surfaces.empty()) {
    for (std::size_t i=0;i<ranges.size();++i) examine(i);
  } else {
    for (auto i:selected_surfaces) examine(i);
  }
  if(ambiguous) return {SurfaceTextureResult::Status::ambiguous_support,{}};
  return out;
}
} // namespace

SurfaceTextureResult SurfaceTextureQuery::at_support(
    const hlclient::movement::LocalPlayerMovementState& state,
    const collision::BrushCollisionScene* brushes) const noexcept {
  if (!scene_ || !scene_->world_package()) return {};
  const auto& ground=state.ground_state();
  if (!ground.grounded() || !ground.hit())
    return {SurfaceTextureResult::Status::invalid_support,{}};
  auto foot=ground.contact_position();
  foot.z+=state.hull()==hlclient::movement::PlayerMovementHull::standing
      ? movement::kValveStandingHullMinimumZ : movement::kValveDuckHullMinimumZ;
  AssetVector3 start{foot.x,foot.y,foot.z+2.0F};
  AssetVector3 end{foot.x,foot.y,foot.z-4.0F};
  if (!finite(start) || !finite(end))
    return {SurfaceTextureResult::Status::invalid_support,{}};
  try {
    if (ground.hit()->kind==hlclient::movement::PlayerMovementHitKind::world &&
        ground.hit()->source_model_index==0U) {
      // SDK presentation trace depth. Collision has already proved support;
      // the longer read-only ray permits hull-supported slopes, not new physics.
      end.z=foot.z-64.0F;
      if(!world_query_.package()) return {};
      // Compiled clip hulls are independent BSP trees: subtracting an analytic
      // player-box expansion cannot recover their authored hull-0 plane. Trace
      // the immutable point hull instead, then require render-plane agreement.
      // This presentation query never changes the proved player support.
      hlclient::collision::CollisionTraceRequest request;
      request.start=start; request.end=end;
      const auto trace=world_query_.trace_line(request,scratch_);
      if(!trace || trace.result->start_solid || trace.result->all_solid ||
          trace.result->in_liquid || !trace.result->collision_plane ||
          !trace.result->hit || trace.result->hit->source_model_index!=0U)
        return {SurfaceTextureResult::Status::invalid_support,{}};
      const auto& plane=*trace.result->collision_plane;
      if(plane.normal.z<=0 || dot(plane.normal,ground.plane().normal)<.9999)
        return {SurfaceTextureResult::Status::invalid_support,{}};
      auto result=query_package(*scene_->world_package(),{},start,end,
          plane.normal,plane.distance);
      result.source_model_index=0U;
      return result;
    }
    if (ground.hit()->kind!=hlclient::movement::PlayerMovementHitKind::brush_entity ||
        !ground.hit()->stable_instance_ordinal || !brushes)
      return {SurfaceTextureResult::Status::invalid_support,{}};
    const auto instance=std::find_if(brushes->instances().begin(),
        brushes->instances().end(),[&](const auto& value) {
          return value.identity.stable_instance_ordinal==*ground.hit()->stable_instance_ordinal &&
              value.identity.source_model_index==ground.hit()->source_model_index &&
              value.identity.source_entity_index==ground.hit()->source_entity_index &&
              value.role==collision::BrushCollisionRole::solid;
        });
    if (instance==brushes->instances().end() ||
        !brush_models::valid_brush_rigid_transform(instance->transform))
      return {SurfaceTextureResult::Status::invalid_support,{}};
    const auto& library=scene_->brush_library();
    const auto model=std::find_if(library.models().begin(),library.models().end(),
        [&](const auto& value) {return value.source_model_index()==ground.hit()->source_model_index;});
    if (model==library.models().end() || !library.render_package())
      return {};
    if (model->render_surface_indices().empty()) return {};
    start=brush_models::brush_rigid_world_to_local_point(instance->transform,start);
    end=brush_models::brush_rigid_world_to_local_point(instance->transform,end);
    if (!finite(start) || !finite(end)) return {};
    auto result=query_package(*library.render_package(),model->render_surface_indices(),start,end);
    result.source_model_index=ground.hit()->source_model_index;
    if(result.world_origin) result.world_origin=brush_models::brush_rigid_local_to_world_point(
        instance->transform,*result.world_origin);
    return result;
  } catch (...) {
    return {};
  }
}
} // namespace hlclient::goldsrc
