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
    const movement::LocalMovementCollisionQueryConfig& config = {}) {
  const auto old=collision.test_position(origin,hull,scratch,config);
  if (!old) return false;
  if (old.result->status==movement::LocalMovementPositionStatus::free) return true;
  auto candidate=origin;
  candidate.z=std::nextafter(candidate.z,std::numeric_limits<float>::infinity());
  const auto clear=collision.test_position(candidate,hull,scratch,config);
  if (!clear || clear.result->status!=movement::LocalMovementPositionStatus::free) return false;
  origin=candidate;
  return true;
}
} // namespace
ReferenceBrushCollisionContext build_reference_brush_collision(
    std::shared_ptr<const collision::BrushCollisionModelLibrary> library,
    const std::map<std::uint32_t, std::string> &model_names,
    const client::RuntimeClientObservationState &observation,
    const std::uint64_t expected_generation) {
  ReferenceBrushCollisionContext out;
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
    if (!entity.model_index || *entity.model_index == 0U)
      continue;
    const auto binding = model_names.find(*entity.model_index);
    if (binding == model_names.end()) {
      out.reason = "collision_model_slot_unavailable";
      return out;
    }
    if (binding->second.empty() || binding->second.front() != '*')
      continue;
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
          kReferenceGoldSrcButtonUse))!=0U ||
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
  const bool jump=(buttons&kReferenceGoldSrcButtonJump)!=0U;
  if (jump) velocity={ladder_policy->jump_away_speed*n.x,
      ladder_policy->jump_away_speed*n.y,0.0F};
  else if (f!=0.0F || r!=0.0F) {
    const assets::AssetVector3 wish{forward.x*f+right.x*r,
        forward.y*f+right.y*r,forward.z*f+right.z*r};
    const float into=wish.x*n.x+wish.y*n.y;
    velocity={wish.x-into*n.x,wish.y-into*n.y,wish.z-into};
  }
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
  info.view_angles={command.view_angles()[0U],command.view_angles()[1U],
      command.view_angles()[2U]};
  info.mode=jump ? PlayerMovementMode::airborne : PlayerMovementMode::ladder;
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
  const movement::BrushSceneMovementCollision start_collision{start}, end_collision{end};
  auto start_info=hlclient::movement::local_player_movement_state_create_info(previous);
  if (start_info.ground.grounded && start_info.ground.hit &&
      start_info.ground.hit->kind==hlclient::movement::PlayerMovementHitKind::brush_entity) {
    const auto old_z=start_info.origin.z;
    if (!support_clearance(start_info.origin,start_info.hull,start_collision,scratch.collision,config.collision_query)) return failed();
    start_info.ground.contact_position.z+=start_info.origin.z-old_z;
  }
  const auto start_state=hlclient::movement::LocalPlayerMovementState::create(start_info,config.state_limits);
  if (!start_state) return failed();
  auto result = simulate_ladder_or_walk(*start_state.state,command,environment,
      start_collision,scratch,config,ladder_context,ladder_policy);
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
          swept.result->trace.fraction < 1.0) return failed();
      info.origin = request.end;
      info.ground.contact_position.z += dz;
      info.ground.plane.distance += info.ground.plane.normal.z * dz;
    }
  }
  if (info.ground.grounded && info.ground.hit &&
      info.ground.hit->kind==hlclient::movement::PlayerMovementHitKind::brush_entity) {
    const auto old_z=info.origin.z;
    if (!support_clearance(info.origin,info.hull,end_collision,scratch.collision,config.collision_query)) return failed();
    info.ground.contact_position.z+=info.origin.z-old_z;
  } else {
    const auto clear = end_collision.test_position(info.origin,info.hull,scratch.collision,config.collision_query);
    if (!clear || clear.result->status != movement::LocalMovementPositionStatus::free) return failed();
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
    if (out.scene && from.ground_state().grounded() && from.ground_state().hit() &&
        from.ground_state().hit()->kind==hlclient::movement::PlayerMovementHitKind::brush_entity) {
      movement::BrushSceneMovementCollision collision{out.scene};
      (void)support_clearance(out.trace_start,from.hull(),collision,scratch,config);
      if (to.ground_state().grounded() && to.ground_state().hit()==from.ground_state().hit())
        (void)support_clearance(out.origin,to.hull(),collision,scratch,config);
    }
  }
  return out;
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
