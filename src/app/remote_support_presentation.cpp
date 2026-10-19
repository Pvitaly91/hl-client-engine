#include <hlclient/app/remote_support_presentation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hlclient::app {
namespace {
namespace gc = goldsrc::collision;
using Actor = client::RuntimePacketEntityObservation;
using Context = goldsrc::ReferenceBrushCollisionContext;

bool finite(const client::RuntimeVector3Observation& value) noexcept {
  return value.complete() && std::isfinite(*value.x) && std::isfinite(*value.y) &&
      std::isfinite(*value.z) && std::abs(*value.x) <= 1048576.0 &&
      std::abs(*value.y) <= 1048576.0 && std::abs(*value.z) <= 1048576.0;
}
assets::AssetVector3 vector(const client::RuntimeVector3Observation& value) noexcept {
  return {static_cast<float>(*value.x),static_cast<float>(*value.y),static_cast<float>(*value.z)};
}
const Actor* actor(const client::RuntimeClientObservationState& state, std::uint32_t entity) noexcept {
  const auto found=std::ranges::find(state.packet_entities,entity,&Actor::entity_number);
  return found==state.packet_entities.end() ? nullptr : &*found;
}
bool eligible(const Actor& value) noexcept {
  return value.player_movement_schema && value.move_type==3U &&
      value.use_hull && *value.use_hull<=1U && value.spectator==false &&
      value.model_index && *value.model_index && value.weapon_model_index &&
      *value.weapon_model_index && (value.effects.value_or(0U)&128U)==0U && finite(value.origin);
}
const gc::BrushCollisionSceneInstance* instance(
    const gc::BrushCollisionScene& scene, const gc::BrushCollisionInstanceIdentity& id) noexcept {
  const auto found=std::ranges::find(scene.instances(),id,&gc::BrushCollisionSceneInstance::identity);
  return found==scene.instances().end() || found->role!=gc::BrushCollisionRole::solid ? nullptr : &*found;
}
bool same_vertical_space(const gc::BrushCollisionSceneInstance& a,
    const gc::BrushCollisionSceneInstance& b) noexcept {
  const auto& x=a.transform;const auto& y=b.transform;
  return a.identity==b.identity && x.translation.x==y.translation.x &&
      x.translation.y==y.translation.y && x.rotation_degrees.x==y.rotation_degrees.x &&
      x.rotation_degrees.y==y.rotation_degrees.y && x.rotation_degrees.z==y.rotation_degrees.z &&
      x.source_model_origin.x==y.source_model_origin.x &&
      x.source_model_origin.y==y.source_model_origin.y && x.source_model_origin.z==y.source_model_origin.z &&
      x.coordinate_profile==y.coordinate_profile && x.transform_profile==y.transform_profile &&
      std::isfinite(x.translation.z) && std::isfinite(y.translation.z);
}
std::optional<gc::BrushCollisionInstanceIdentity> ground(
    const std::shared_ptr<const gc::BrushCollisionScene>& scene, assets::AssetVector3 origin,
    std::uint32_t use_hull, collision::CollisionQueryScratch& scratch) {
  gc::BrushCollisionSceneTraceRequest request;
  request.start=origin;request.end=origin;request.end.z-=2.0F;
  request.hull=use_hull==0U ? collision::CollisionHullOrdinal::standing_32x32x72 :
      collision::CollisionHullOrdinal::duck_32x32x36;
  const auto hit=gc::BrushCollisionSceneQuery{scene}.trace_hull(request,scratch);
  if(!hit || !hit.result || !hit.result->scene_hit || !hit.result->scene_hit->brush_instance)
    return {};
  const auto& trace=hit.result->trace;
  if(trace.start_solid || trace.all_solid || trace.in_liquid || trace.fraction>=1.0 ||
      !trace.collision_plane || !std::isfinite(trace.collision_plane->normal.z) ||
      trace.collision_plane->normal.z<.7F) return {};
  // Near-plane support, not a two-unit magnet for falling/jumping actors.
  const auto& plane=*trace.collision_plane;
  const double distance=(plane.normal.x*origin.x+plane.normal.y*origin.y+
      plane.normal.z*origin.z-plane.distance)/plane.normal.z;
  if(!std::isfinite(distance) || distance<-.0001 || distance>.125) return {};
  return hit.result->scene_hit->brush_instance;
}
bool pusher(const Context& context,const gc::BrushCollisionInstanceIdentity& id) noexcept {
  return std::ranges::find(context.pushers,id)!=context.pushers.end();
}
bool possible_support(const Context& context,const Actor& value) noexcept {
  const double foot=*value.origin.z-(*value.use_hull==0U ? 36.0 : 18.0);
  for(const auto& item:context.scene->instances()) {
    if(item.role!=gc::BrushCollisionRole::solid || !pusher(context,item.identity)) continue;
    const auto& b=item.transformed_bounds;
    // Rejection only: an AABB overlap never establishes contact. Retain a
    // fractional-unit margin for the exact source hull trace that follows.
    if(*value.origin.x+16.125>=b.minimum.x && *value.origin.x-16.125<=b.maximum.x &&
        *value.origin.y+16.125>=b.minimum.y && *value.origin.y-16.125<=b.maximum.y &&
        foot>=b.minimum.z-.125 && foot<=b.maximum.z+.125) return true;
  }
  return false;
}
}

std::optional<RemoteSupportPresentation> sample_remote_vertical_support(
    const goldsrc::RuntimePlayerInterpolationAnchor& anchor, const Actor& sampled,
    const client::RuntimeClientObservationState& latest, std::span<const Context> contexts,
    const std::shared_ptr<const gc::BrushCollisionScene>& rendered,
    collision::CollisionQueryScratch& scratch) {
  if(anchor.discontinuity || !anchor.previous || !anchor.current || !rendered ||
      !rendered->model_library() || contexts.size()<2U || contexts.size()>17U ||
      anchor.entity!=sampled.entity_number || !eligible(sampled) ||
      !std::isfinite(anchor.sample_seconds) || latest.generation==0U) return {};
  const auto* before=actor(*anchor.previous,anchor.entity);
  const auto* after=actor(*anchor.current,anchor.entity);
  if(!before || !after || !eligible(*before) || !eligible(*after) ||
      before->model_index!=after->model_index || after->model_index!=sampled.model_index ||
      before->use_hull!=after->use_hull || after->use_hull!=sampled.use_hull ||
      anchor.previous->generation!=latest.generation || anchor.current->generation!=latest.generation)
    return {};
  const auto a=std::ranges::find(contexts,anchor.previous->publication_revision,&Context::revision);
  const auto b=std::ranges::find(contexts,anchor.current->publication_revision,&Context::revision);
  if(a==contexts.end() || b==contexts.end() || a>=b || !a->scene || !b->scene ||
      !a->server_time || !b->server_time || !contexts.back().server_time || !contexts.back().scene ||
      contexts.back().revision!=latest.publication_revision ||
      contexts.back().scene->model_library()!=rendered->model_library()) return {};
  const double dt=*b->server_time-*a->server_time;
  if(!std::isfinite(dt) || dt<.001 || dt>.250 ||
      anchor.sample_seconds<*a->server_time || anchor.sample_seconds>*b->server_time ||
      *contexts.back().server_time-anchor.sample_seconds>.250) return {};
  if(!possible_support(*a,*before) || !possible_support(*b,*after)) return {};
  const auto old_id=ground(a->scene,vector(before->origin),*before->use_hull,scratch);
  const auto next_id=ground(b->scene,vector(after->origin),*after->use_hull,scratch);
  if(!old_id || old_id!=next_id) return {};
  const auto* old_support=instance(*a->scene,*old_id);
  const auto* next_support=instance(*b->scene,*old_id);
  const auto* final_support=instance(*rendered,*old_id);
  if(!old_support || !next_support || !final_support) return {};
  const auto* previous_support=old_support;
  const Context* previous_context=nullptr;
  for(auto it=a;it!=contexts.end();++it) {
    if(it->generation!=latest.generation || !it->scene || !it->server_time ||
        !std::isfinite(*it->server_time) || !pusher(*it,*old_id) ||
        it->scene->model_library()!=rendered->model_library()) return {};
    const auto* support=instance(*it->scene,*old_id);
    if(!support || !same_vertical_space(*old_support,*support)) return {};
    if(previous_context) {
      const double elapsed=*it->server_time-*previous_context->server_time;
      const double displacement=support->transform.translation.z-previous_support->transform.translation.z;
      if(it->revision<=previous_context->revision || elapsed<0.0 || elapsed>.250 ||
          (elapsed==0.0 ? displacement!=0.0 : std::abs(displacement)>256.0*elapsed+.001)) return {};
    }
    previous_context=&*it;previous_support=support;
  }
  if(!same_vertical_space(*previous_support,*final_support) ||
      std::abs(final_support->transform.translation.z-previous_support->transform.translation.z)>64.001)
    return {};
  const double old_relative=*before->origin.z-old_support->transform.translation.z;
  const double new_relative=*after->origin.z-next_support->transform.translation.z;
  if(std::abs(old_relative-new_relative)>.125) return {};
  const double alpha=(anchor.sample_seconds-*a->server_time)/dt;
  const double support_z=old_support->transform.translation.z+
      alpha*(next_support->transform.translation.z-old_support->transform.translation.z);
  const double expected_relative=old_relative+alpha*(new_relative-old_relative);
  if(std::abs(*sampled.origin.z-support_z-expected_relative)>.125) return {};
  const double dz=final_support->transform.translation.z-support_z;
  if(!std::isfinite(dz) || std::abs(dz)>128.0) return {};
  auto presented=vector(sampled.origin);presented.z=static_cast<float>(presented.z+dz);
  gc::BrushCollisionSceneTraceRequest carry;
  carry.start=vector(sampled.origin);carry.end=presented;
  carry.ignored_instance=*old_id;
  carry.hull=*sampled.use_hull==0U ? collision::CollisionHullOrdinal::standing_32x32x72 :
      collision::CollisionHullOrdinal::duck_32x32x36;
  const auto path=gc::BrushCollisionSceneQuery{rendered}.trace_hull(carry,scratch);
  if(!path || !path.result || path.result->trace.start_solid || path.result->trace.all_solid ||
      path.result->trace.in_liquid || path.result->trace.fraction<1.0) return {};
  // The final complete rendered scene must prove both clearance and contact;
  // nearby platforms, swept-through ceilings and running off an edge cannot
  // acquire this render offset. No geometry is loosened to make it fit.
  if(ground(rendered,presented,*sampled.use_hull,scratch)!=old_id) return {};
  return RemoteSupportPresentation{presented,*old_id,anchor.sample_seconds,
      support_z,final_support->transform.translation.z,dz};
}
} // namespace hlclient::app
