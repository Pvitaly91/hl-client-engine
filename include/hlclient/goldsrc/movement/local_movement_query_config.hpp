#pragma once

#include <hlclient/collision/collision_query_limits.hpp>
#include <hlclient/goldsrc/collision/brush_collision_scene_limits.hpp>

namespace hlclient::goldsrc::movement {

struct LocalMovementCollisionQueryConfig {
    hlclient::collision::CollisionQueryLimits query_limits{};
    hlclient::collision::CollisionTraceToleranceProfile trace_tolerance{};
    hlclient::goldsrc::collision::BrushCollisionSceneQueryLimits scene_limits{};
};

[[nodiscard]] bool valid_local_movement_collision_query_config(
    const LocalMovementCollisionQueryConfig& config) noexcept;

} // namespace hlclient::goldsrc::movement
