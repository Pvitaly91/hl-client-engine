#pragma once
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <map>
#include <string>

namespace hlclient::goldsrc {
// One owning current-server-frame context for a complete replay transaction.
// No renderer state, extrapolated transform, retained RX view or local pusher.
struct ReferenceBrushCollisionContext {
  std::shared_ptr<const collision::BrushCollisionScene> scene;
  std::uint64_t generation{}, revision{}, record{};
  std::size_t solid_count{}, non_solid_count{};
  std::optional<double> server_time;
  std::vector<collision::BrushCollisionInstanceIdentity> pushers;
  // Nonblocking server-marked CONTENTS_LADDER brush hulls retained in this
  // exact committed entity frame. Never inferred from a map classname alone.
  std::vector<collision::BrushCollisionInstanceIdentity> ladders;
  std::string_view reason{"collision_entities_unavailable"};
};
[[nodiscard]] ReferenceBrushCollisionContext build_reference_brush_collision(
    std::shared_ptr<const collision::BrushCollisionModelLibrary> library,
    const std::map<std::uint32_t, std::string> &model_names,
    const client::RuntimeClientObservationState &observation,
    std::uint64_t expected_generation);

struct ReferenceLadderContact {
  collision::BrushCollisionInstanceIdentity identity{};
  assets::AssetVector3 outward_normal{};
};
// Valve PM_Ladder's model-hull membership, then an independently traced
// horizontal BSP entry plane for PM_LadderMove. This query never adds the
// ladder to the ordinary blocking scene or trusts a texture/classname.
[[nodiscard]] std::optional<ReferenceLadderContact> query_reference_ladder_contact(
    const ReferenceBrushCollisionContext& context,
    assets::AssetVector3 origin,
    hlclient::movement::PlayerMovementHull hull,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::LocalMovementCollisionQueryConfig& config = {});

// Owning, transaction-local DERIVED trajectory, never canonical evidence.
// Two same-generation svc_time/entity publications; vertical rigid PUSH only.
// Extrapolation is clamped to 250 ms, speed to 256 units/s. No receipt clock.
class ReferenceVerticalSupportMotion final {
public:
  ReferenceVerticalSupportMotion(const ReferenceBrushCollisionContext& previous,
      const ReferenceBrushCollisionContext& current);
  [[nodiscard]] bool active() const noexcept { return !velocities_.empty(); }
  [[nodiscard]] std::shared_ptr<const collision::BrushCollisionScene>
      sample(double server_seconds) const;
  [[nodiscard]] float displacement(const hlclient::movement::PlayerMovementHitIdentity& hit,
      double from_seconds, double to_seconds) const noexcept;
private:
  ReferenceBrushCollisionContext current_;
  double oldest_time_{};
  std::vector<std::pair<collision::BrushCollisionInstanceIdentity, double>> velocities_;
};

// Same entry point for append and replay. The seed already contains all
// server-applied displacement. Only the command's own time interval is carried;
// velocity/basevelocity and command bytes are not changed. Jump/edge/world
// contact detaches. Blocked carry fails atomically (no client crush authority).
[[nodiscard]] movement::LocalMovementSimulationResult simulate_reference_movement(
    const hlclient::movement::LocalPlayerMovementState& previous,
    const GoldSrcUserCmdState& command,
    const movement::GoldSrcMovementEnvironment& environment,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config,
    const ReferenceVerticalSupportMotion* support = nullptr,
    const ReferenceBrushCollisionContext* ladder_context = nullptr,
    const movement::ReferenceLadderMovementPolicy* ladder_policy = nullptr);

struct ReferenceSupportPresentation {
  assets::AssetVector3 origin{}, trace_start{};
  double server_seconds{};
  std::shared_ptr<const collision::BrushCollisionScene> scene;
};
// Render-only sample: never updates history, canonical state or TX. The sweep
// start is expressed at the sampled support time; otherwise a rising brush
// would falsely classify the older interpolation endpoint as startsolid.
[[nodiscard]] ReferenceSupportPresentation sample_reference_support_presentation(
    const hlclient::movement::LocalPlayerMovementState& from,
    const hlclient::movement::LocalPlayerMovementState& to, double alpha,
    const ReferenceVerticalSupportMotion* support,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::LocalMovementCollisionQueryConfig& config = {});

// A render-only fallback for a blocked straight chord between two verified
// world-grounded prediction endpoints. Re-sample the same walkable BSP support
// below the intermediate XY and require a clear raised sweep. It never changes
// the command history, physical endpoint, correction or collision tolerance.
[[nodiscard]] std::optional<assets::AssetVector3>
recover_reference_ground_presentation(
    const hlclient::movement::LocalPlayerMovementState& from,
    const hlclient::movement::LocalPlayerMovementState& to, double alpha,
    const movement::LocalMovementTrace& blocked,
    const movement::ILocalMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config);
} // namespace hlclient::goldsrc
