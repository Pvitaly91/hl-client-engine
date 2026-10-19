#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/reference_client_move.hpp>

namespace hlclient::goldsrc {
namespace {
class Roles final : public collision::IBrushCollisionRoleProvider {
public:
  std::vector<collision::SyntheticBrushCollisionRoleBinding> bindings;
  collision::BrushCollisionRoleProviderProfile
  profile() const noexcept override {
    return collision::BrushCollisionRoleProviderProfile::
        reference_entity_solid_bsp_v1;
  }
  collision::BrushCollisionRole
  role_for(const collision::BrushCollisionInstanceIdentity &id)
      const noexcept override {
    const auto found =
        std::find_if(bindings.begin(), bindings.end(),
                     [&](const auto &b) { return b.identity == id; });
    return found == bindings.end()
               ? collision::BrushCollisionRole::evidence_pending
               : found->role;
  }
};
bool finite(const client::RuntimeVector3Observation &v) {
  return v.complete() && std::isfinite(*v.x) && std::isfinite(*v.y) &&
         std::isfinite(*v.z);
}
assets::AssetVector3 vector(const client::RuntimeVector3Observation &v) {
  return {static_cast<float>(*v.x), static_cast<float>(*v.y),
          static_cast<float>(*v.z)};
}
// Only generated support positions, never a wire seed: translation roundoff
// can put a boundary point one binary32 ULP inside its support. Accept a single
// outward step only if the complete scene proves the resulting hull free.
bool support_clearance(assets::AssetVector3& origin, hlclient::movement::PlayerMovementHull hull,
    const movement::BrushSceneMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::LocalMovementCollisionQueryConfig& config = {},
    movement::GoldSrcLocalMovementScratch* observation = nullptr) {
  const auto old=collision.test_position(origin,hull,scratch,config);
  if (!old) return false;
  if (old.result->status==movement::LocalMovementPositionStatus::free) return true;
  const auto candidate=movement::resolve_movement_boundary_position(origin,hull,collision,scratch,config);
  if (!candidate) {
    if (observation) {
      observation->last_reference_collision_position = old.result;
      observation->last_reference_collision_hull = hull;
      observation->last_reference_collision_start = origin;
      observation->last_reference_collision_end = origin;
    }
    return false;
  }
  origin=*candidate;
  return true;
}

struct SampledSupportPlane {
  const collision::BrushCollisionSceneInstance* instance{};
  double distance{};
};

// Ground planes are retained collision witnesses, not a fresh trajectory.
// Resolve their exact source plane in the current sampled brush instead of
// adding a new velocity estimate to an older generated endpoint plane.
std::optional<SampledSupportPlane> sampled_support_plane(
    const collision::BrushCollisionScene& scene,
    const hlclient::movement::PlayerMovementHitIdentity& hit,
    const hlclient::movement::PlayerMovementPlane& ground) {
  if (hit.kind != hlclient::movement::PlayerMovementHitKind::brush_entity ||
      !hit.stable_instance_ordinal || !hit.source_entity_index ||
      !ground.source_plane_index || !scene.model_library()) return {};
  const auto found = std::find_if(scene.instances().begin(), scene.instances().end(),
      [&](const auto& instance) { return instance.role == collision::BrushCollisionRole::solid &&
          instance.identity.source_model_index == hit.source_model_index &&
          instance.identity.source_entity_index == hit.source_entity_index &&
          instance.identity.stable_instance_ordinal == *hit.stable_instance_ordinal; });
  if (found == scene.instances().end()) return {};
  const auto* model = scene.model_library()->model(hit.source_model_index);
  if (!model || !model->collision_world) return {};
  const auto planes = model->collision_world->planes();
  const auto ordinal = *ground.source_plane_index;
  if (ordinal >= planes.size() || planes[ordinal].source_plane_index != ordinal) return {};
  const auto& plane = planes[ordinal];
  const auto normal = brush_models::brush_rigid_local_to_world_normal(found->transform, plane.normal);
  const auto matches = [&](const double sign) { return
      normal.x * sign == ground.normal.x && normal.y * sign == ground.normal.y &&
      normal.z * sign == ground.normal.z; };
  const double sign = matches(1.0) ? 1.0 : matches(-1.0) ? -1.0 : 0.0;
  if (sign == 0.0) return {};
  const auto& translation = found->transform.translation;
  const double distance = sign * plane.distance +
      static_cast<double>(ground.normal.x) * translation.x +
      static_cast<double>(ground.normal.y) * translation.y +
      static_cast<double>(ground.normal.z) * translation.z;
  if (!std::isfinite(distance)) return {};
  return SampledSupportPlane{&*found, distance};
}

// A newly committed pusher trajectory can put an already generated contact
// slightly below its newly projected top. Re-express only that exact retained
// top contact, never a raw authority seed or an arbitrary penetrating hull.
// Both brush contacts and the entire vertical path through all OTHER solids
// must be proven. The support remains solid in the final occupancy check.
bool refresh_generated_support_contact(
    hlclient::movement::LocalPlayerMovementStateCreateInfo& info,
    const movement::BrushSceneMovementCollision& sampled,
    const ReferenceVerticalSupportMotion& support,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config) {
  namespace player = hlclient::movement;
  if (info.state_revision <= 1U || info.mode != player::PlayerMovementMode::walking ||
      !info.ground.grounded || !info.ground.walkable || !info.ground.hit ||
      info.velocity.z != 0.0F || info.in_duck_transition ||
      info.ground.plane.normal.x != 0.0F || info.ground.plane.normal.y != 0.0F ||
      info.ground.plane.normal.z != 1.0F || !sampled.scene()) return false;
  const auto speed = support.velocity_z_for(*info.ground.hit);
  if (!speed || *speed <= 0.0) return false;
  const auto plane = sampled_support_plane(*sampled.scene(), *info.ground.hit, info.ground.plane);
  if (!plane) return false;
  const double rise = plane->distance - info.ground.plane.distance;
  if (!std::isfinite(rise) || rise <= 0.0 || rise > 256.0 * .050) return false;
  const auto* model = sampled.scene()->model_library()->model(info.ground.hit->source_model_index);
  const auto hull = movement::local_movement_collision_hull(info.hull);
  if (!model || !hull) return false;
  collision::ExplicitBrushCollisionTraceRequest request;
  request.start = info.origin; request.end = info.origin; request.end.z -= 2.0F;
  request.hull = *hull;
  request.query_limits = config.collision_query.query_limits;
  request.tolerance = config.collision_query.trace_tolerance;
  auto previous_pose = plane->instance->transform;
  previous_pose.translation.z = static_cast<float>(previous_pose.translation.z - rise);
  const auto old_contact = collision::trace_explicit_brush_model(
      *model, previous_pose, request, scratch.collision);
  const auto matches_contact = [&](const auto& traced, const double distance) {
    return traced && traced.result && !traced.result->trace.start_solid &&
        !traced.result->trace.all_solid && !traced.result->trace.in_liquid &&
        traced.result->trace.fraction < 1.0 && traced.result->trace.collision_plane &&
        traced.result->trace.collision_plane->source_plane_index == info.ground.plane.source_plane_index &&
        traced.result->trace.collision_plane->normal.x == 0.0F &&
        traced.result->trace.collision_plane->normal.y == 0.0F &&
        traced.result->trace.collision_plane->normal.z == 1.0F &&
        // Origins and instance translations are binary32; carried plane
        // distances retain double addition. Require the same representable
        // plane, not a widened gameplay/penetration epsilon.
        static_cast<float>(traced.result->trace.collision_plane->distance) == static_cast<float>(distance);
  };
  if (!matches_contact(old_contact, info.ground.plane.distance)) return false;
  // Do not repair an unrelated overlap whose first reported hit happens to
  // be this support. Full-scene clearance and a swept hull retain those hits.
  auto candidate = info.origin;
  candidate.z = static_cast<float>(candidate.z + rise);
  if (!support_clearance(candidate, info.hull, sampled, scratch.collision,
          config.collision_query, &scratch) || candidate.x != info.origin.x ||
      candidate.y != info.origin.y) return false;
  request.start = candidate; request.end = candidate; request.end.z -= 2.0F;
  if (!matches_contact(collision::trace_explicit_brush_model(
          *model, plane->instance->transform, request, scratch.collision), plane->distance)) return false;
  auto instances = std::vector<collision::BrushCollisionSceneInstance>{
      sampled.scene()->instances().begin(), sampled.scene()->instances().end()};
  for (auto& instance : instances)
    if (instance.identity == plane->instance->identity) instance.role = collision::BrushCollisionRole::non_solid;
  const auto others_scene = std::make_shared<const collision::BrushCollisionScene>(
      sampled.scene()->model_library(), std::move(instances), sampled.scene()->role_provider_profile());
  const movement::BrushSceneMovementCollision others{others_scene, sampled.entity_boxes()};
  const auto swept = others.trace_hull(info.origin, candidate, info.hull,
      scratch.collision, config.collision_query);
  if (!swept || !swept.result || swept.result->start_solid || swept.result->all_solid ||
      swept.result->in_liquid || swept.result->fraction < 1.0) {
    scratch.last_reference_collision_position.reset();
    scratch.last_reference_collision_trace = swept.result;
    scratch.last_reference_collision_hull = info.hull;
    scratch.last_reference_collision_start = info.origin;
    scratch.last_reference_collision_end = candidate;
    return false;
  }
  info.origin = candidate;
  info.ground.contact_position = candidate;
  info.ground.plane.distance = plane->distance;
  scratch.last_reference_collision_position.reset();
  scratch.last_reference_collision_trace.reset();
  scratch.last_reference_collision_hull.reset();
  scratch.last_reference_collision_start.reset();
  scratch.last_reference_collision_end.reset();
  return true;
}

// A tied world floor can win ground categorization while the hull still
// overlaps a rising platform. Ground identity alone then misses the platform's
// contact during this command and the final occupancy check suspends replay.
// Acquire only a newly arriving, witnessed top; an existing penetration, side
// contact or blocked push is not a recoverable support handoff.
bool rising_support_contact(hlclient::movement::LocalPlayerMovementStateCreateInfo& info,
    const movement::LocalMovementPositionTest& blocked,
    const movement::BrushSceneMovementCollision& start_collision,
    const movement::BrushSceneMovementCollision& end_collision,
    const ReferenceVerticalSupportMotion& support, const double from, const double to,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config) {
  namespace player = hlclient::movement;
  if (!info.ground.grounded || !info.ground.walkable || !info.ground.hit ||
      info.ground.hit->kind != player::PlayerMovementHitKind::world ||
      info.mode != player::PlayerMovementMode::walking || info.velocity.z != 0.0F ||
      blocked.status != movement::LocalMovementPositionStatus::blocking ||
      blocked.contents.category != player::PlayerMovementContents::solid ||
      !blocked.hit || blocked.hit->kind != player::PlayerMovementHitKind::brush_entity ||
      !blocked.hit->source_entity_index || !blocked.hit->stable_instance_ordinal)
    return false;
  const float rise = support.displacement(*blocked.hit, from, to);
  // Same 256 units/s and 50 ms command limits as the existing carry path.
  if (!std::isfinite(rise) || rise <= 0.0F || rise > 12.8F) return false;
  const auto is_free = [&](const movement::LocalMovementPositionQueryResult& position) {
    return position && position.result &&
        position.result->status == movement::LocalMovementPositionStatus::free &&
        position.result->contents.category == player::PlayerMovementContents::empty &&
        !position.result->hit;
  };
  if (!is_free(start_collision.test_position(info.origin, info.hull, scratch, config.collision_query)))
    return false;
  // One outward binary32 neighbour accommodates transform addition rounding;
  // no collision epsilon or penetration budget is widened.
  auto free_side = info.origin;
  free_side.z = std::nextafter(static_cast<float>(info.origin.z + rise),
      std::numeric_limits<float>::infinity());
  if (!std::isfinite(free_side.z) ||
      !is_free(end_collision.test_position(free_side, info.hull, scratch, config.collision_query)))
    return false;
  const auto entry = end_collision.trace_hull(free_side, info.origin, info.hull,
      scratch, config.collision_query);
  if (!entry || !entry.result || entry.result->start_solid || entry.result->all_solid ||
      entry.result->in_liquid || !std::isfinite(entry.result->fraction) ||
      entry.result->fraction < 0.0 || entry.result->fraction >= 1.0 ||
      entry.result->hit != blocked.hit || !entry.result->collision_plane ||
      entry.result->collision_plane->normal.z < config.minimum_walkable_normal_z)
    return false;
  const auto& plane = *entry.result->collision_plane;
  if (!std::isfinite(plane.normal.x) || !std::isfinite(plane.normal.y) ||
      !std::isfinite(plane.normal.z) || !std::isfinite(plane.distance)) return false;
  auto candidate = info.origin;
  candidate.z = static_cast<float>((plane.distance -
      static_cast<double>(plane.normal.x) * candidate.x -
      static_cast<double>(plane.normal.y) * candidate.y) / plane.normal.z);
  if (!is_free(end_collision.test_position(candidate, info.hull, scratch, config.collision_query))) {
    candidate.z = std::nextafter(candidate.z, std::numeric_limits<float>::infinity());
    if (!is_free(end_collision.test_position(candidate, info.hull, scratch, config.collision_query)))
      return false;
  }
  if (!std::isfinite(candidate.z) || candidate.z <= info.origin.z || candidate.z > free_side.z)
    return false;
  const collision::BrushCollisionInstanceIdentity identity{
      *blocked.hit->stable_instance_ordinal, blocked.hit->source_model_index,
      blocked.hit->source_entity_index};
  const auto& end_scene = end_collision.scene();
  auto other_instances = std::vector<collision::BrushCollisionSceneInstance>{
      end_scene->instances().begin(), end_scene->instances().end()};
  const auto contacting = std::find_if(other_instances.begin(), other_instances.end(),
      [&](const auto& instance) { return instance.identity == identity &&
          instance.role == collision::BrushCollisionRole::solid; });
  if (contacting == other_instances.end()) return false;
  contacting->role = collision::BrushCollisionRole::non_solid;
  const auto other_scene = std::make_shared<const collision::BrushCollisionScene>(
      end_scene->model_library(), std::move(other_instances), end_scene->role_provider_profile());
  const movement::BrushSceneMovementCollision others{other_scene, end_collision.entity_boxes()};
  const auto carried = others.trace_hull(info.origin, candidate, info.hull, scratch, config.collision_query);
  if (!carried || !carried.result || carried.result->start_solid || carried.result->all_solid ||
      carried.result->in_liquid || carried.result->fraction < 1.0)
    return false;
  info.origin = candidate;
  info.ground.hit = blocked.hit;
  info.ground.plane = plane;
  info.ground.contact_position = candidate;
  info.ground.probe_fraction = entry.result->fraction;
  return true;
}
} // namespace
ReferenceBrushCollisionContext build_reference_brush_collision(
    std::shared_ptr<const collision::BrushCollisionModelLibrary> library,
    const std::map<std::uint32_t, std::string> &model_names,
    const client::RuntimeClientObservationState &observation,
    const std::uint64_t expected_generation,
    const std::optional<std::uint32_t> server_box_solid_code,
    const std::optional<std::uint32_t> receiving_entity, const bool exclude_owned_boxes,
    const std::optional<std::uint32_t> server_player_solid_code,
    const std::span<const movement::ServerPointBoxExclusionRule> nonblocking_point_boxes) {
  ReferenceBrushCollisionContext out;
  if (!movement::valid_server_point_box_exclusion_rules(nonblocking_point_boxes)) {
    out.reason="collision_point_box_policy_invalid"; return out;
  }
  if (!library || !library->collision_world() || expected_generation == 0U ||
      observation.generation != expected_generation ||
      observation.entity_metadata.generation != expected_generation ||
      (observation.entity_metadata.freshness !=
           client::RuntimeObservationFreshness::retained &&
       observation.entity_metadata.freshness !=
           client::RuntimeObservationFreshness::observed_in_record) ||
      observation.entity_metadata.completeness !=
          client::RuntimeObservationCompleteness::complete_reconstruction ||
      !observation.entity_metadata.source ||
      observation.entity_metadata.source->record_identity == 0U ||
      observation.publication_revision == 0U ||
      observation.packet_entities.size() > 8192U)
    return out;
  out.generation = observation.generation;
  out.revision = observation.publication_revision;
  out.record = observation.entity_metadata.source->record_identity;
  if (observation.server_time_seconds && std::isfinite(*observation.server_time_seconds) &&
      *observation.server_time_seconds >= 0.0 &&
      observation.server_time_metadata.source &&
      observation.server_time_metadata.generation == expected_generation &&
      observation.server_time_metadata.source->record_identity == out.record)
    out.server_time = observation.server_time_seconds;
  std::vector<collision::BrushCollisionInstanceDefinition> instances;
  Roles roles;
  for (const auto &entity : observation.packet_entities) {
    const bool player_box=server_player_solid_code && entity.player_movement_schema &&
        entity.solid==server_player_solid_code;
    if (player_box || (server_box_solid_code && entity.solid == server_box_solid_code)) {
        if (entity.entity_number == receiving_entity) continue;
        if (!player_box && exclude_owned_boxes && receiving_entity && entity.owner_entity_reference == receiving_entity)
          continue;
        if (!finite(entity.origin) || !finite(entity.collision_mins) || !finite(entity.collision_maxs) ||
            (!player_box && exclude_owned_boxes && !entity.owner_entity_reference)) {
          out.reason = player_box ? "collision_player_box_fields_unavailable" : "collision_box_fields_unavailable"; return out;
        }
        const auto origin=vector(entity.origin), low=vector(entity.collision_mins), high=vector(entity.collision_maxs);
        const assets::WorldBounds bounds{{origin.x+low.x,origin.y+low.y,origin.z+low.z},
            {origin.x+high.x,origin.y+high.y,origin.z+high.z}};
        const auto valid_axis=[](float low_value,float high_value) {
          return std::isfinite(low_value) && std::isfinite(high_value) && low_value<=high_value &&
              std::abs(low_value)<=1'000'000.F && std::abs(high_value)<=1'000'000.F && high_value-low_value<=8192.F;
        };
        if (!valid_axis(bounds.minimum.x,bounds.maximum.x) || !valid_axis(bounds.minimum.y,bounds.maximum.y) ||
            !valid_axis(bounds.minimum.z,bounds.maximum.z) || !entity.entity_number) {
          out.reason="collision_box_bounds_invalid"; return out;
        }
        // Compare the negotiated doubles, before float conversion: a tiny but
        // nonzero extent must not acquire a point-box exemption by underflow.
        const auto zero=[](const client::RuntimeVector3Observation& value) {
          return *value.x==0.0 && *value.y==0.0 && *value.z==0.0;
        };
        if (!entity.player_movement_schema && zero(entity.collision_mins) &&
            zero(entity.collision_maxs) && entity.model_index && *entity.model_index>0U && entity.brush_move_type &&
            !nonblocking_point_boxes.empty()) {
          const auto name=model_names.find(*entity.model_index);
          if (name!=model_names.end() && std::any_of(nonblocking_point_boxes.begin(),
              nonblocking_point_boxes.end(),[&](const auto& rule) {
                return rule.model_name==name->second && rule.move_type==*entity.brush_move_type;
              })) {
            ++out.excluded_point_box_count;
            continue;
          }
        }
        if (out.boxes.size()>=movement::kMaximumServerEntityCollisionBoxes) {
          out.reason="collision_box_limit"; return out;
        }
        out.boxes.push_back({entity.entity_number,bounds});
      continue;
    }
    if (!entity.model_index || *entity.model_index == 0U)
      continue;
    const auto binding = model_names.find(*entity.model_index);
    if (binding == model_names.end()) {
      out.reason = "collision_model_slot_unavailable";
      return out;
    }
    if (binding->second.empty() || binding->second.front() != '*') continue;
    const auto ref = brush_models::parse_brush_model_reference(
        binding->second, library->collision_world()->models().size());
    if (!ref || *ref.source_model_index == 0U) {
      out.reason = "collision_brush_reference_invalid";
      return out;
    }
    if (!entity.solid) {
      out.reason = "collision_solid_field_unavailable";
      return out;
    }
    // Valve CLadder publishes SOLID_NOT, MOVETYPE_PUSH and skin=-16
    // (CONTENTS_LADDER). Its retained BSP hull is queryable by PM_Ladder,
    // but must never join the blocking collision union.
    const bool ladder = *entity.solid == 0U && entity.skin == -16 &&
        entity.brush_move_type == 7U;
    if (*entity.solid == 0U || *entity.solid == 1U) {
      ++out.non_solid_count;
      if (!ladder) continue;
    }
    if (*entity.solid != 4U && !ladder) {
      out.reason = "collision_solid_role_unsupported";
      return out;
    }
    if (!ladder && (!entity.brush_move_type ||
        (*entity.brush_move_type != 0U && *entity.brush_move_type != 7U))) {
      out.reason = "collision_brush_movetype_unsupported";
      return out;
    }
    if (!finite(entity.origin) || !finite(entity.angles)) {
      out.reason = "collision_transform_unavailable";
      return out;
    }
    const auto *model = library->model(*ref.source_model_index);
    if (!model) {
      out.reason = "collision_model_unavailable";
      return out;
    }
    const auto transform = brush_models::make_brush_rigid_transform(
        vector(entity.origin), vector(entity.angles), model->source_origin);
    if (!transform) {
      out.reason = "collision_transform_unsupported";
      return out;
    }
    collision::BrushCollisionInstanceIdentity identity{
        entity.entity_number, *ref.source_model_index, entity.entity_number};
    instances.push_back({identity, *transform.transform});
    if (ladder) {
      out.ladders.push_back(identity);
      roles.bindings.push_back({identity, collision::BrushCollisionRole::non_solid});
    } else {
      if (*entity.brush_move_type == 7U) out.pushers.push_back(identity);
      roles.bindings.push_back({identity, collision::BrushCollisionRole::solid});
      ++out.solid_count;
    }
  }
  const auto built = collision::build_brush_collision_scene(std::move(library),
                                                            instances, roles);
  if (!built) {
    out.reason = "collision_scene_build_failed";
    return out;
  }
  out.scene = built.scene;
  out.reason = "current_server_frame_no_pusher_extrapolation";
  return out;
}

std::optional<ReferenceLadderContact> query_reference_ladder_contact(
    const ReferenceBrushCollisionContext& context,
    const assets::AssetVector3 origin,
    const hlclient::movement::PlayerMovementHull hull,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::LocalMovementCollisionQueryConfig& config) {
  if (!context.scene || context.ladders.empty() ||
      !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
    return std::nullopt;
  const auto ordinal=movement::local_movement_collision_hull(hull);
  if (!ordinal) return std::nullopt;
  const auto& library=*context.scene->model_library();
  std::optional<ReferenceLadderContact> best;
  double best_distance=std::numeric_limits<double>::infinity();
  for (const auto& identity : context.ladders) {
    const auto instance=std::find_if(context.scene->instances().begin(),
        context.scene->instances().end(),[&](const auto& item) {
          return item.identity==identity &&
              item.role==collision::BrushCollisionRole::non_solid;
        });
    if (instance==context.scene->instances().end()) continue;
    const auto* model=library.model(identity.source_model_index);
    if (!model) continue;
    collision::ExplicitBrushCollisionTraceRequest request;
    request.start=origin; request.end=origin; request.hull=*ordinal;
    request.tolerance=config.trace_tolerance;
    request.query_limits=config.query_limits;
    const auto membership=collision::trace_explicit_brush_model(
        *model,instance->transform,request,scratch);
    if (!membership || !membership.result ||
        !membership.result->trace.start_solid) continue;
    const auto& bounds=instance->transformed_bounds;
    const assets::AssetVector3 center{
        (bounds.minimum.x+bounds.maximum.x)*.5F,
        (bounds.minimum.y+bounds.maximum.y)*.5F,
        std::clamp(origin.z,bounds.minimum.z,bounds.maximum.z)};
    constexpr float outside=32.0F; // farther than the stock standing half-hull
    const std::array<assets::AssetVector3,4> starts{{
        {bounds.minimum.x-outside,center.y,center.z},
        {bounds.maximum.x+outside,center.y,center.z},
        {center.x,bounds.minimum.y-outside,center.z},
        {center.x,bounds.maximum.y+outside,center.z}}};
    for (const auto& start : starts) {
      request.start=start; request.end=center;
      const auto traced=collision::trace_explicit_brush_model(
          *model,instance->transform,request,scratch);
      if (!traced || !traced.result || traced.result->trace.start_solid ||
          traced.result->trace.all_solid ||
          !traced.result->trace.collision_plane ||
          traced.result->trace.fraction>=1.0) continue;
      const auto& plane=*traced.result->trace.collision_plane;
      if (std::abs(plane.normal.z)>=.7F) continue;
      const double distance=std::abs(
          static_cast<double>(plane.normal.x)*origin.x+
          static_cast<double>(plane.normal.y)*origin.y+
          static_cast<double>(plane.normal.z)*origin.z-plane.distance);
      if (distance<best_distance) {
        best_distance=distance;
        best=ReferenceLadderContact{identity,plane.normal};
      }
    }
  }
  return best;
}

ReferenceVerticalSupportMotion::ReferenceVerticalSupportMotion(
    const ReferenceBrushCollisionContext& previous,
    const ReferenceBrushCollisionContext& current) : current_(current) {
  if (!previous.scene || !current.scene || previous.generation != current.generation ||
      previous.scene->model_library() != current.scene->model_library() ||
      !previous.server_time || !current.server_time || previous.record >= current.record)
    return;
  const auto dt = *current.server_time - *previous.server_time;
  if (dt < .001 || dt > .250) return;
  oldest_time_ = *previous.server_time;
  for (const auto& a : current.scene->instances()) {
    if (std::find(current.pushers.begin(), current.pushers.end(), a.identity) == current.pushers.end() ||
        std::find(previous.pushers.begin(), previous.pushers.end(), a.identity) == previous.pushers.end()) continue;
    const auto old = std::find_if(previous.scene->instances().begin(), previous.scene->instances().end(),
        [&](const auto& b) { return a.identity == b.identity; });
    if (old == previous.scene->instances().end()) continue;
    const auto& x = a.transform; const auto& y = old->transform;
    if (x.translation.x != y.translation.x || x.translation.y != y.translation.y ||
        x.rotation_degrees.x != y.rotation_degrees.x || x.rotation_degrees.y != y.rotation_degrees.y ||
        x.rotation_degrees.z != y.rotation_degrees.z) continue;
    const double speed = (static_cast<double>(x.translation.z) - y.translation.z) / dt;
    if (std::isfinite(speed) && std::abs(speed) <= 256.0)
      velocities_.emplace_back(a.identity, speed);
  }
}

bool ReferenceVerticalSupportMotion::refresh_same_time_context(
    const ReferenceBrushCollisionContext& current) {
  if (!active() || !current_.scene || !current.scene ||
      current_.generation != current.generation ||
      current_.scene->model_library() != current.scene->model_library() ||
      !current_.server_time || !current.server_time ||
      !std::isfinite(*current.server_time) || current_.server_time != current.server_time ||
      current.record == 0U || current.record == current_.record)
    return false;
  const auto same_vector = [](const auto& a, const auto& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
  };
  std::vector<std::pair<collision::BrushCollisionInstanceIdentity, double>> retained;
  retained.reserve(velocities_.size());
  for (const auto& velocity : velocities_) {
    const auto& identity = velocity.first;
    if (std::find(current.pushers.begin(), current.pushers.end(), identity) == current.pushers.end())
      continue;
    const auto old = std::find_if(current_.scene->instances().begin(), current_.scene->instances().end(),
        [&](const auto& instance) { return instance.identity == identity; });
    const auto next = std::find_if(current.scene->instances().begin(), current.scene->instances().end(),
        [&](const auto& instance) { return instance.identity == identity; });
    if (old == current_.scene->instances().end() || next == current.scene->instances().end() ||
        old->role != collision::BrushCollisionRole::solid || next->role != old->role ||
        !same_vector(old->transform.translation, next->transform.translation) ||
        !same_vector(old->transform.rotation_degrees, next->transform.rotation_degrees))
      continue;
    retained.push_back(velocity);
  }
  // Allocate the complete replacement before changing either retained owner.
  auto replacement = current;
  current_ = std::move(replacement);
  velocities_ = std::move(retained);
  return true;
}

std::shared_ptr<const collision::BrushCollisionScene>
ReferenceVerticalSupportMotion::sample(const double seconds) const {
  if (!active() || !std::isfinite(seconds)) return current_.scene;
  const auto delta = std::clamp(seconds, oldest_time_, *current_.server_time + .250) - *current_.server_time;
  if (delta == 0.0 || std::none_of(velocities_.begin(),velocities_.end(),
      [](const auto& v) { return v.second!=0.0; })) return current_.scene;
  std::vector<collision::BrushCollisionInstanceDefinition> instances;
  Roles roles;
  for (const auto& instance : current_.scene->instances()) {
    auto transform = instance.transform;
    for (const auto& [identity, speed] : velocities_)
      if (identity == instance.identity)
        transform.translation.z = static_cast<float>(transform.translation.z + speed * delta);
    instances.push_back({instance.identity, transform});
    roles.bindings.push_back({instance.identity, instance.role});
  }
  return collision::build_brush_collision_scene(current_.scene->model_library(), instances, roles).scene;
}

std::optional<double> ReferenceVerticalSupportMotion::velocity_z_for(
    const hlclient::movement::PlayerMovementHitIdentity& hit) const noexcept {
  if (hit.kind != hlclient::movement::PlayerMovementHitKind::brush_entity) return {};
  for (const auto& [id, speed] : velocities_)
    if (hit.source_entity_index == id.source_entity_index &&
        hit.source_model_index == id.source_model_index &&
        hit.stable_instance_ordinal == id.stable_instance_ordinal)
      return speed;
  return {};
}

float ReferenceVerticalSupportMotion::displacement(
    const hlclient::movement::PlayerMovementHitIdentity& hit, double from, double to) const noexcept {
  if (!active() || !std::isfinite(from) || !std::isfinite(to) ||
      hit.kind != hlclient::movement::PlayerMovementHitKind::brush_entity) return 0.0F;
  const auto dt = std::clamp(to, oldest_time_, *current_.server_time + .250) -
      std::clamp(from, oldest_time_, *current_.server_time + .250);
  for (const auto& [id, speed] : velocities_)
    if (hit.source_entity_index == id.source_entity_index &&
        hit.source_model_index == id.source_model_index &&
        hit.stable_instance_ordinal == id.stable_instance_ordinal)
      return static_cast<float>(speed * dt);
  return 0.0F;
}

namespace {
movement::LocalMovementSimulationResult simulate_ladder_or_walk(
    const hlclient::movement::LocalPlayerMovementState& previous,
    const GoldSrcUserCmdState& command,
    const movement::GoldSrcMovementEnvironment& environment,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config,
    const ReferenceBrushCollisionContext* ladder_context,
    const movement::ReferenceLadderMovementPolicy* ladder_policy) {
  using hlclient::movement::PlayerMovementMode;
  const auto contact=ladder_context && ladder_policy ? query_reference_ladder_contact(
      *ladder_context,previous.origin(),previous.hull(),scratch.collision,
      config.collision_query) : std::nullopt;
  if (!contact) {
    if (previous.mode()!=PlayerMovementMode::ladder)
      return movement::GoldSrcLocalMovementKernel::simulate(previous,command,
          environment,collision,scratch,config);
    // The next command falls through the ordinary kernel after leaving the
    // server-marked hull; no retained ladder identity or local timer survives.
    auto leaving=hlclient::movement::local_player_movement_state_create_info(previous);
    leaving.mode=PlayerMovementMode::airborne;
    leaving.ground={};
    const auto state=hlclient::movement::LocalPlayerMovementState::create(
        leaving,config.state_limits);
    if (state) return movement::GoldSrcLocalMovementKernel::simulate(
        *state.state,command,environment,collision,scratch,config);
  }
  auto fail=[](movement::LocalMovementSimulationErrorCode code,
               std::string_view why) {
    movement::LocalMovementSimulationResult result;
    result.error=movement::LocalMovementSimulationError{code,{},why};
    return result;
  };
  if (!contact || !movement::valid_goldsrc_local_movement_config(config) ||
      !std::isfinite(ladder_policy->maximum_climb_speed) ||
      ladder_policy->maximum_climb_speed<=0.0F ||
      ladder_policy->maximum_climb_speed>1'000.0F ||
      !std::isfinite(ladder_policy->duck_speed_multiplier) ||
      ladder_policy->duck_speed_multiplier<=0.0F ||
      ladder_policy->duck_speed_multiplier>1.0F ||
      !std::isfinite(ladder_policy->jump_away_speed) ||
      ladder_policy->jump_away_speed<=0.0F ||
      ladder_policy->jump_away_speed>1'000.0F ||
      command.command_sequence().value()==0U ||
      previous.source_command_sequence()==UINT32_MAX ||
      command.command_sequence().value()!=previous.source_command_sequence()+1U ||
      command.msec()==0U || command.msec()>50U || command.up_move()!=0.0F ||
      command.impulse()!=0U ||
      (command.buttons() & ~(kReferenceGoldSrcButtonDirections |
          kReferenceGoldSrcButtonJump | kReferenceGoldSrcButtonDuck |
          kReferenceGoldSrcButtonAttack | kReferenceGoldSrcButtonReload |
          kReferenceGoldSrcButtonUse | kReferenceGoldSrcButtonAttack2 |
          (command.compatibility_profile()==GoldSrcUserCmdCompatibilityProfile::
               public_goldsrc48_jump_duck_weapon_use_prediction_v4
           ? kReferenceGoldSrcButtonScore : 0U)))!=0U ||
      previous.command_profile()!=hlclient::movement::GoldSrcMovementCommandProfile::
          reference_wire_jump_duck_v2 ||
      (command.compatibility_profile()!=GoldSrcUserCmdCompatibilityProfile::
           public_goldsrc48_jump_duck_prediction_v2 &&
       command.compatibility_profile()!=GoldSrcUserCmdCompatibilityProfile::
           public_goldsrc48_jump_duck_weapon_prediction_v3 &&
       command.compatibility_profile()!=GoldSrcUserCmdCompatibilityProfile::
           public_goldsrc48_jump_duck_weapon_use_prediction_v4) ||
      environment.profile()!=movement::GoldSrcMovementEnvironmentProfile::
          movevars_dry_walk_subset_v1)
    return fail(movement::LocalMovementSimulationErrorCode::invalid_state,
        "ladder command or context is not executable");
  constexpr float pi=3.14159265358979323846F;
  const float yaw=command.view_angles()[1U]*(pi/180.0F);
  const float pitch=command.view_angles()[0U]*(pi/180.0F);
  if (!std::isfinite(yaw) || !std::isfinite(pitch))
    return fail(movement::LocalMovementSimulationErrorCode::invalid_state,
        "ladder view angles are non-finite");
  const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
  const assets::AssetVector3 forward{cp*cy,cp*sy,-sp};
  const assets::AssetVector3 right{sy,-cy,0.0F};
  const auto normal=contact->outward_normal;
  const float horizontal=std::hypot(normal.x,normal.y);
  if (!(horizontal>.7F))
    return fail(movement::LocalMovementSimulationErrorCode::invalid_state,
        "ladder face is not horizontal");
  const assets::AssetVector3 n{normal.x/horizontal,normal.y/horizontal,0.0F};
  float speed=std::min(ladder_policy->maximum_climb_speed,environment.maximum_speed());
  if ((command.buttons()&kReferenceGoldSrcButtonDuck)!=0U ||
      previous.hull()==hlclient::movement::PlayerMovementHull::ducked)
    speed*=ladder_policy->duck_speed_multiplier;
  const auto buttons=command.buttons();
  const float f=speed*static_cast<float>(
      ((buttons&kReferenceGoldSrcButtonForward)!=0U)-
      ((buttons&kReferenceGoldSrcButtonBack)!=0U));
  const float r=speed*static_cast<float>(
      ((buttons&kReferenceGoldSrcButtonMoveRight)!=0U)-
      ((buttons&kReferenceGoldSrcButtonMoveLeft)!=0U));
  assets::AssetVector3 velocity{};
  const auto impulse=command.velocity_impulse();
  const bool has_impulse=std::ranges::any_of(impulse,[](float v){return v!=0;});
  const bool jump=(buttons&kReferenceGoldSrcButtonJump)!=0U;
  if (jump) velocity={ladder_policy->jump_away_speed*n.x,
      ladder_policy->jump_away_speed*n.y,0.0F};
  else if (f!=0.0F || r!=0.0F) {
    const assets::AssetVector3 wish{forward.x*f+right.x*r,
        forward.y*f+right.y*r,forward.z*f+right.z*r};
    const float into=wish.x*n.x+wish.y*n.y;
    velocity={wish.x-into*n.x,wish.y-into*n.y,wish.z-into};
  }
  if(has_impulse)velocity={velocity.x+impulse[0],velocity.y+impulse[1],velocity.z+impulse[2]};
  const float dt=static_cast<float>(command.msec())*.001F;
  const assets::AssetVector3 target{previous.origin().x+velocity.x*dt,
      previous.origin().y+velocity.y*dt,previous.origin().z+velocity.z*dt};
  const auto trace=collision.trace_hull(previous.origin(),target,previous.hull(),
      scratch.collision,config.collision_query);
  if (!trace || !trace.result)
    return fail(movement::LocalMovementSimulationErrorCode::movement_trace_failed,
        "ladder world/brush sweep failed");
  auto info=hlclient::movement::local_player_movement_state_create_info(previous);
  if (trace.result->start_solid || trace.result->all_solid)
    return fail(movement::LocalMovementSimulationErrorCode::movement_trace_failed,
        "ladder player starts in blocking solid");
  info.origin=trace.result->end_position;
  if (trace.result->fraction<1.0) velocity={};
  info.velocity=velocity;
  const auto& post_impulse=command.post_velocity_impulse();
  info.velocity.x+=post_impulse[0];info.velocity.y+=post_impulse[1];info.velocity.z+=post_impulse[2];
  info.view_angles={command.view_angles()[0U],command.view_angles()[1U],
      command.view_angles()[2U]};
  info.mode=(jump || has_impulse) ? PlayerMovementMode::airborne : PlayerMovementMode::ladder;
  info.ground={};
  info.old_buttons=buttons;
  info.source_command_sequence=command.command_sequence().value();
  const auto increment=static_cast<std::uint64_t>(command.msec())*1'000'000U;
  if (previous.simulation_time_nanoseconds()>UINT64_MAX-increment ||
      previous.state_revision()>=config.state_limits.maximum_state_revision)
    return fail(movement::LocalMovementSimulationErrorCode::state_revision_exhausted,
        "ladder movement state identity exhausted");
  info.simulation_time_nanoseconds+=increment;
  ++info.state_revision;
  const auto created=hlclient::movement::LocalPlayerMovementState::create(
      info,config.state_limits);
  if (!created) return fail(movement::LocalMovementSimulationErrorCode::invalid_state,
      "ladder result state failed validation");
  movement::LocalMovementSimulationResult result;
  result.state.emplace(*created.state);
  result.command_sequence=info.source_command_sequence;
  result.deterministic_state_signature=
      hlclient::movement::local_player_movement_state_signature(*result.state);
  result.statistics.command_count=1U;
  result.statistics.substep_count=1U;
  result.statistics.trace_count=1U;
  result.statistics.airborne_command_count=1U;
  result.statistics.jump_count=jump;
  result.statistics.total_horizontal_distance=std::hypot(
      static_cast<double>(info.origin.x)-previous.origin().x,
      static_cast<double>(info.origin.y)-previous.origin().y);
  result.statistics.total_vertical_distance=std::abs(
      static_cast<double>(info.origin.z)-previous.origin().z);
  return result;
}
} // namespace

movement::LocalMovementSimulationResult simulate_reference_movement(
    const hlclient::movement::LocalPlayerMovementState& previous,
    const GoldSrcUserCmdState& command,
    const movement::GoldSrcMovementEnvironment& environment,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config,
    const ReferenceVerticalSupportMotion* support,
    const ReferenceBrushCollisionContext* ladder_context,
    const movement::ReferenceLadderMovementPolicy* ladder_policy) {
  scratch.last_reference_collision_trace.reset();
  scratch.last_reference_collision_position.reset();
  scratch.last_reference_collision_hull.reset();
  scratch.last_reference_collision_start.reset();
  scratch.last_reference_collision_end.reset();
  if (!support || !support->active())
    return simulate_ladder_or_walk(previous,command,environment,collision,scratch,
        config,ladder_context,ladder_policy);
  const double from = static_cast<double>(previous.simulation_time_nanoseconds()) * 1e-9;
  const double to = from + static_cast<double>(command.msec()) * .001;
  const auto start = support->sample(from), end = support->sample(to);
  auto failed = [] {
    movement::LocalMovementSimulationResult result;
    result.error = movement::LocalMovementSimulationError{
        movement::LocalMovementSimulationErrorCode::movement_trace_failed, {}, "derived support carry blocked"};
    return result;
  };
  if (!start || !end) return failed();
  const movement::BrushSceneMovementCollision start_collision{start,support->entity_boxes()},
      end_collision{end,support->entity_boxes()};
  auto start_info=hlclient::movement::local_player_movement_state_create_info(previous);
  if (start_info.ground.grounded && start_info.ground.hit &&
      start_info.ground.hit->kind==hlclient::movement::PlayerMovementHitKind::brush_entity) {
    const auto old_origin=start_info.origin;
    if (!support_clearance(start_info.origin,start_info.hull,start_collision,scratch.collision,config.collision_query,&scratch)) {
      if ((command.buttons() & kReferenceGoldSrcButtonJump) != 0U ||
          !refresh_generated_support_contact(start_info,start_collision,*support,scratch,config)) return failed();
    } else {
      start_info.ground.contact_position.x+=start_info.origin.x-old_origin.x;
      start_info.ground.contact_position.y+=start_info.origin.y-old_origin.y;
      start_info.ground.contact_position.z+=start_info.origin.z-old_origin.z;
    }
  }
  const auto start_state=hlclient::movement::LocalPlayerMovementState::create(start_info,config.state_limits);
  if (!start_state) return failed();
  auto result = [&]() {
    auto attempted = simulate_ladder_or_walk(*start_state.state,command,environment,
        start_collision,scratch,config,ladder_context,ladder_policy);
    // A world contact can win a coplanar top tie at the preceding command.
    // A newer pusher trajectory then crosses it before this command starts.
    // Inspect only a failed initial zero-length occupancy query: the failed
    // attempt has not simulated movement, so no accepted command is replayed.
    const auto same_origin = [&](const auto& origin) { return origin &&
        origin->x == start_info.origin.x && origin->y == start_info.origin.y &&
        origin->z == start_info.origin.z; };
    const auto source_time = support->source_time_seconds();
    if (attempted || !attempted.error ||
        (attempted.error->code != movement::LocalMovementSimulationErrorCode::player_startsolid &&
         attempted.error->code != movement::LocalMovementSimulationErrorCode::player_allsolid) ||
        start_info.state_revision <= 1U || !start_info.ground.grounded ||
        !start_info.ground.hit || start_info.ground.hit->kind != hlclient::movement::PlayerMovementHitKind::world ||
        start_info.in_duck_transition || (command.buttons() & kReferenceGoldSrcButtonJump) != 0U ||
        !source_time || !std::isfinite(*source_time) || from <= *source_time || from - *source_time > .250 ||
        !scratch.last_reference_collision_position ||
        !same_origin(scratch.last_reference_collision_start) || !same_origin(scratch.last_reference_collision_end))
      return attempted;
    const auto observed_scene = support->sample(*source_time);
    if (!observed_scene) return attempted;
    const movement::BrushSceneMovementCollision observed{observed_scene,support->entity_boxes()};
    auto reacquired = start_info;
    if (!rising_support_contact(reacquired, *scratch.last_reference_collision_position,
            observed, start_collision, *support, *source_time, from, scratch.collision, config))
      return attempted;
    const auto repaired = hlclient::movement::LocalPlayerMovementState::create(reacquired,config.state_limits);
    if (!repaired) return attempted;
    scratch.last_reference_collision_position.reset();
    scratch.last_reference_collision_trace.reset();
    scratch.last_reference_collision_hull.reset();
    scratch.last_reference_collision_start.reset();
    scratch.last_reference_collision_end.reset();
    return simulate_ladder_or_walk(*repaired.state,command,environment,
        start_collision,scratch,config,ladder_context,ladder_policy);
  }();
  if (!result) return result;
  auto info = hlclient::movement::local_player_movement_state_create_info(*result.state);
  if (info.ground.grounded && info.ground.hit) {
    const auto dz = support->displacement(*info.ground.hit,from,to);
    if (dz != 0.0F) {
      collision::BrushCollisionSceneTraceRequest request;
      request.start = info.origin; request.end = info.origin; request.end.z += dz;
      request.hull = *movement::local_movement_collision_hull(info.hull);
      request.query_limits = config.collision_query.query_limits;
      request.tolerance = config.collision_query.trace_tolerance;
      request.scene_limits = config.collision_query.scene_limits;
      const auto& hit = *info.ground.hit;
      request.ignored_instance = collision::BrushCollisionInstanceIdentity{
          hit.stable_instance_ordinal.value_or(0U), hit.source_model_index, hit.source_entity_index};
      const auto swept = collision::BrushCollisionSceneQuery{end}.trace_hull(request,scratch.collision);
      if (!swept || swept.result->trace.start_solid || swept.result->trace.all_solid ||
          swept.result->trace.fraction < 1.0) {
        scratch.last_reference_collision_hull = info.hull;
        scratch.last_reference_collision_start = request.start;
        scratch.last_reference_collision_end = request.end;
        if (swept.result) {
          const auto& trace = swept.result->trace;
          movement::LocalMovementTrace observed;
          observed.start_solid = trace.start_solid; observed.all_solid = trace.all_solid;
          observed.fraction = trace.fraction; observed.end_position = trace.end_position;
          observed.in_open = trace.in_open; observed.in_liquid = trace.in_liquid;
          observed.collision_profile = movement::LocalMovementCollisionProfile::reference_brush_scene_v1;
          if (trace.collision_plane) observed.collision_plane = hlclient::movement::PlayerMovementPlane{
              trace.collision_plane->normal, trace.collision_plane->distance, trace.collision_plane->source_plane_index};
          if (swept.result->scene_hit) {
            const auto& scene_hit = *swept.result->scene_hit;
            observed.hit = scene_hit.brush_instance ? hlclient::movement::PlayerMovementHitIdentity{
                hlclient::movement::PlayerMovementHitKind::brush_entity,
                scene_hit.brush_instance->source_model_index, scene_hit.brush_instance->stable_instance_ordinal,
                scene_hit.brush_instance->source_entity_index} : hlclient::movement::PlayerMovementHitIdentity{
                hlclient::movement::PlayerMovementHitKind::world, 0U, {}, {}};
          }
          if (const auto contents = movement::normalize_local_movement_contents(trace.start_contents))
            observed.start_contents = *contents;
          if (const auto contents = movement::normalize_local_movement_contents(trace.end_contents))
            observed.end_contents = *contents;
          if (trace.blocking_contents) observed.blocking_contents = movement::normalize_local_movement_contents(*trace.blocking_contents);
          observed.traversal_statistics = {trace.traversal_statistics.point_contents_steps,
              trace.traversal_statistics.traversal_steps, trace.traversal_statistics.maximum_stack_entries,
              trace.traversal_statistics.fraction_split_count, trace.traversal_statistics.terminal_interval_count};
          scratch.last_reference_collision_trace = observed;
        }
        return failed();
      }
      info.origin = request.end;
      info.ground.contact_position.z += dz;
      info.ground.plane.distance += info.ground.plane.normal.z * dz;
    }
  }
  if (info.ground.grounded && info.ground.hit &&
      info.ground.hit->kind==hlclient::movement::PlayerMovementHitKind::brush_entity) {
    const auto old_origin=info.origin;
    if (!support_clearance(info.origin,info.hull,end_collision,scratch.collision,config.collision_query,&scratch)) return failed();
    info.ground.contact_position.x+=info.origin.x-old_origin.x;
    info.ground.contact_position.y+=info.origin.y-old_origin.y;
    info.ground.contact_position.z+=info.origin.z-old_origin.z;
  } else {
    const auto clear = end_collision.test_position(info.origin,info.hull,scratch.collision,config.collision_query);
    if ((!clear || clear.result->status != movement::LocalMovementPositionStatus::free) &&
        (!clear || !start_info.ground.grounded ||
         !rising_support_contact(info, *clear.result, start_collision, end_collision,
             *support, from, to, scratch.collision, config))) {
      scratch.last_reference_collision_position = clear.result;
      scratch.last_reference_collision_hull = info.hull;
      scratch.last_reference_collision_start = info.origin;
      scratch.last_reference_collision_end = info.origin;
      return failed();
    }
  }
  const auto created = hlclient::movement::LocalPlayerMovementState::create(info,config.state_limits);
  if (!created) return failed();
  result.state.emplace(*created.state);
  result.statistics.total_vertical_distance = std::abs(static_cast<double>(info.origin.z) - previous.origin().z);
  result.deterministic_state_signature = hlclient::movement::local_player_movement_state_signature(*result.state);
  return result;
}
ReferenceSupportPresentation sample_reference_support_presentation(
    const hlclient::movement::LocalPlayerMovementState& from,
    const hlclient::movement::LocalPlayerMovementState& to, double alpha,
    const ReferenceVerticalSupportMotion* support,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::LocalMovementCollisionQueryConfig& config) {
  ReferenceSupportPresentation out;
  alpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0, 1.0) : 0.0;
  const auto& a = from.origin(); const auto& b = to.origin();
  out.origin = {static_cast<float>(a.x+(b.x-a.x)*alpha),
      static_cast<float>(a.y+(b.y-a.y)*alpha),static_cast<float>(a.z+(b.z-a.z)*alpha)};
  out.trace_start = a;
  const double start = static_cast<double>(from.simulation_time_nanoseconds())*1e-9;
  out.server_seconds = start + (static_cast<double>(to.simulation_time_nanoseconds())*1e-9-start)*alpha;
  if (support && support->active()) {
    out.scene = support->sample(out.server_seconds);
    if (from.ground_state().grounded() && from.ground_state().hit())
      out.trace_start.z += support->displacement(*from.ground_state().hit(),start,out.server_seconds);
    // A newly observed stop may re-time the corrected anchor while H3 retains
    // its immediately preceding command state. The new motion object's frozen
    // scene is not the older command's support pose. Those states already own
    // witnessed world-space planes: interpolate their matching contact for the
    // render-only scene, without continuing the old physical carry or claiming
    // exact server history. Other scene objects and BBOX blockers stay current.
    const auto& old_ground=from.ground_state();
    const auto& new_ground=to.ground_state();
    if (out.scene && from.hull()==to.hull() && old_ground.grounded() &&
        new_ground.grounded() && old_ground.walkable() && new_ground.walkable() &&
        old_ground.hit() && new_ground.hit()==old_ground.hit() &&
        old_ground.hit()->kind==hlclient::movement::PlayerMovementHitKind::brush_entity &&
        old_ground.hit()->stable_instance_ordinal && old_ground.hit()->source_entity_index &&
        old_ground.plane().source_plane_index &&
        old_ground.plane().source_plane_index==new_ground.plane().source_plane_index &&
        old_ground.plane().normal.x==new_ground.plane().normal.x &&
        old_ground.plane().normal.y==new_ground.plane().normal.y &&
        old_ground.plane().normal.z==new_ground.plane().normal.z &&
        old_ground.plane().normal.z>0.F &&
        to.simulation_time_nanoseconds()>from.simulation_time_nanoseconds() &&
        to.simulation_time_nanoseconds()-from.simulation_time_nanoseconds()<=250'000'000ULL &&
        support->velocity_z_for(*old_ground.hit())) {
      const auto& hit=*old_ground.hit();
      const double normal_z=old_ground.plane().normal.z;
      const double old_plane=old_ground.plane().distance,new_plane=new_ground.plane().distance;
      // Existing reference commands are at most50ms and this support profile
      // admits at most256units/s. A larger contact change is a discontinuity.
      constexpr double maximum_contact_translation=256.0*.050;
      const double contact_delta=(new_plane-old_plane)/normal_z;
      if (std::isfinite(old_plane) && std::isfinite(new_plane) &&
          std::isfinite(contact_delta) && std::abs(contact_delta)<=maximum_contact_translation) {
        const double desired_plane=old_plane+(new_plane-old_plane)*alpha;
        const auto sampled_plane=sampled_support_plane(*out.scene,hit,new_ground.plane());
        const double shift=sampled_plane
            ? (desired_plane-sampled_plane->distance)/normal_z
            : std::numeric_limits<double>::quiet_NaN();
        const auto matching=[&](const auto& instance) {
          return instance.role==collision::BrushCollisionRole::solid &&
              instance.identity.source_model_index==hit.source_model_index &&
              instance.identity.source_entity_index==hit.source_entity_index &&
              instance.identity.stable_instance_ordinal==*hit.stable_instance_ordinal;
        };
        const auto found=std::find_if(out.scene->instances().begin(),out.scene->instances().end(),matching);
        if (std::isfinite(shift) && found!=out.scene->instances().end()) {
          const float sampled_z=static_cast<float>(found->transform.translation.z+shift);
          auto proposed_start=a;
          proposed_start.z=static_cast<float>(proposed_start.z+(desired_plane-old_plane)/normal_z);
          if (std::isfinite(sampled_z) &&
              (sampled_z!=found->transform.translation.z || proposed_start.z!=out.trace_start.z)) {
            auto proposed_scene=out.scene;
            if (sampled_z!=found->transform.translation.z) {
              std::vector<collision::BrushCollisionInstanceDefinition> definitions;
              definitions.reserve(out.scene->instances().size());
              Roles roles; roles.bindings.reserve(out.scene->instances().size());
              for (const auto& instance:out.scene->instances()) {
                auto transform=instance.transform;
                if (matching(instance)) transform.translation.z=sampled_z;
                definitions.push_back({instance.identity,transform});
                roles.bindings.push_back({instance.identity,instance.role});
              }
              proposed_scene=collision::build_brush_collision_scene(
                  out.scene->model_library(),definitions,roles).scene;
            }
            if (proposed_scene) {
              movement::BrushSceneMovementCollision proposed{proposed_scene,support->entity_boxes()};
              auto proposed_origin=out.origin;
              if (support_clearance(proposed_start,from.hull(),proposed,scratch,config) &&
                  support_clearance(proposed_origin,to.hull(),proposed,scratch,config)) {
                const auto clear=proposed.trace_hull(proposed_start,proposed_origin,to.hull(),scratch,config);
                if (clear && clear.result && !clear.result->start_solid &&
                    !clear.result->all_solid && clear.result->fraction>=1.0) {
                  out.scene=std::move(proposed_scene); out.trace_start=proposed_start; out.origin=proposed_origin;
                }
              }
            }
          }
        }
      }
    }
    if (out.scene && from.ground_state().grounded() && from.ground_state().hit() &&
        from.ground_state().hit()->kind==hlclient::movement::PlayerMovementHitKind::brush_entity) {
      movement::BrushSceneMovementCollision collision{out.scene,support->entity_boxes()};
      (void)support_clearance(out.trace_start,from.hull(),collision,scratch,config);
      if (to.ground_state().grounded() && to.ground_state().hit()==from.ground_state().hit())
        (void)support_clearance(out.origin,to.hull(),collision,scratch,config);
    }
  }
  return out;
}

std::optional<ReferenceStepEyePresentation> sample_reference_box_step_eye(
    const hlclient::movement::LocalPlayerMovementState& from,
    const hlclient::movement::LocalPlayerMovementState& to, const double alpha,
    const double maximum_step_height, const movement::ILocalMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config) {
  using Hit = hlclient::movement::PlayerMovementHitKind;
  const auto supported = [](const auto& state) {
    const auto& ground = state.ground_state();
    return ground.grounded() && ground.walkable() && ground.hit() &&
        (ground.hit()->kind == Hit::world || ground.hit()->kind == Hit::server_entity_box);
  };
  if (!std::isfinite(alpha) || alpha <= 0.0 || alpha >= 1.0 ||
      !std::isfinite(maximum_step_height) || maximum_step_height <= 0.0 ||
      maximum_step_height > 64.0 || from.hull() != to.hull() ||
      !supported(from) || !supported(to) ||
      (from.ground_state().hit()->kind != Hit::server_entity_box &&
       to.ground_state().hit()->kind != Hit::server_entity_box)) return {};
  const auto a = from.origin(), b = to.origin();
  if (std::abs(static_cast<double>(b.z) - a.z) <= 1e-5 ||
      std::abs(static_cast<double>(b.z) - a.z) > maximum_step_height + 1e-5 ||
      (a.x == b.x && a.y == b.y)) return {};
  const auto ordinal = movement::local_movement_collision_hull(to.hull());
  const auto hull = ordinal
      ? hlclient::collision::standard_collision_hull_profile(*ordinal) : std::nullopt;
  if (!hull) return {};
  const auto old_offset = from.view_offset(), new_offset = to.view_offset();
  const assets::AssetVector3 raised_a{a.x, a.y, std::max(a.z, b.z)};
  const assets::AssetVector3 raised_b{b.x, b.y, std::max(a.z, b.z)};
  const auto clear = [&](const auto& start, const auto& end) {
    const auto ray = collision.trace_hull(start, end, to.hull(), scratch, config.collision_query);
    return ray && ray.result && !ray.result->start_solid &&
        !ray.result->all_solid && ray.result->fraction >= 1.0;
  };
  // Check the complete legal step route, not only the currently displayed fraction.
  if (!clear(a, raised_a) || !clear(raised_a, raised_b) || !clear(raised_b, b)) return {};
  ReferenceStepEyePresentation result;
  result.origin = {static_cast<float>(a.x + (b.x - a.x) * alpha),
      static_cast<float>(a.y + (b.y - a.y) * alpha), raised_a.z};
  const auto position = collision.test_position(result.origin, to.hull(), scratch, config.collision_query);
  if (!position || !position.result ||
      position.result->status != movement::LocalMovementPositionStatus::free) return {};
  const assets::AssetVector3 eye{
      static_cast<float>(a.x + old_offset.x + (b.x + new_offset.x - a.x - old_offset.x) * alpha),
      static_cast<float>(a.y + old_offset.y + (b.y + new_offset.y - a.y - old_offset.y) * alpha),
      static_cast<float>(a.z + old_offset.z + (b.z + new_offset.z - a.z - old_offset.z) * alpha)};
  result.view_offset = {eye.x - result.origin.x, eye.y - result.origin.y, eye.z - result.origin.z};
  if (result.view_offset.x < hull->clip_mins.x || result.view_offset.x > hull->clip_maxs.x ||
      result.view_offset.y < hull->clip_mins.y || result.view_offset.y > hull->clip_maxs.y ||
      result.view_offset.z <= 0.0 || result.view_offset.z > hull->clip_maxs.z) return {};
  return result;
}

std::optional<assets::AssetVector3> recover_reference_ground_presentation(
    const hlclient::movement::LocalPlayerMovementState& from,
    const hlclient::movement::LocalPlayerMovementState& to, double alpha,
    const movement::LocalMovementTrace& blocked,
    const movement::ILocalMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config) {
  const auto world_grounded=[](const auto& state) {
    return state.ground_state().grounded() && state.ground_state().walkable() &&
        state.ground_state().hit() &&
        state.ground_state().hit()->kind==hlclient::movement::PlayerMovementHitKind::world;
  };
  if (!std::isfinite(alpha) || alpha<=0.0 || alpha>=1.0 ||
      from.hull()!=to.hull() || !world_grounded(from) || !world_grounded(to) ||
      !blocked.collision_plane || blocked.start_solid || blocked.all_solid ||
      blocked.collision_plane->normal.z<config.minimum_walkable_normal_z ||
      !std::isfinite(config.ground_probe_distance) || config.ground_probe_distance<=0.0)
    return std::nullopt;
  const auto& a=from.origin(); const auto& b=to.origin();
  const auto probe=static_cast<float>(config.ground_probe_distance);
  if (std::abs(a.z-b.z)>probe) return std::nullopt; // not a stair/edge smoother
  const assets::AssetVector3 candidate{
      static_cast<float>(a.x+(b.x-a.x)*alpha),
      static_cast<float>(a.y+(b.y-a.y)*alpha),
      static_cast<float>(a.z+(b.z-a.z)*alpha)};
  const assets::AssetVector3 above{candidate.x,candidate.y,
      std::max(a.z,b.z)+probe};
  const assets::AssetVector3 below{candidate.x,candidate.y,
      std::min(a.z,b.z)-probe};
  if (!std::isfinite(above.z) || !std::isfinite(below.z)) return std::nullopt;
  const auto ground=collision.trace_hull(above,below,to.hull(),scratch,config.collision_query);
  if (!ground || !ground.result || ground.result->start_solid || ground.result->all_solid ||
      !ground.result->collision_plane || !ground.result->hit ||
      ground.result->hit->kind!=hlclient::movement::PlayerMovementHitKind::world ||
      ground.result->collision_plane->normal.z<config.minimum_walkable_normal_z)
    return std::nullopt;
  auto resolved=ground.result->end_position;
  bool free=false;
  for (unsigned attempt=0;attempt<5;++attempt) {
    const auto position=collision.test_position(resolved,to.hull(),scratch,config.collision_query);
    if (!position || !position.result) return std::nullopt;
    if (position.result->status==movement::LocalMovementPositionStatus::free) {
      free=true;
      break;
    }
    resolved.z=std::nextafter(resolved.z,std::numeric_limits<float>::infinity());
  }
  if (!free) return std::nullopt;
  // A floor-plane contact can make the unraised tangent chord report fraction
  // zero. Require a parallel sweep above the floor so a wall/ceiling still
  // blocks recovery; the actual presented hull is independently tested free.
  const assets::AssetVector3 raised_from{a.x,a.y,a.z+probe};
  const assets::AssetVector3 raised_to{resolved.x,resolved.y,resolved.z+probe};
  const auto path=collision.trace_hull(raised_from,raised_to,to.hull(),scratch,
      config.collision_query);
  if (!path || !path.result || path.result->start_solid || path.result->all_solid ||
      path.result->fraction<1.0)
    return std::nullopt;
  return resolved;
}
} // namespace hlclient::goldsrc
