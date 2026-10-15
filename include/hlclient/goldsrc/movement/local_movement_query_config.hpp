#pragma once

#include <hlclient/collision/collision_query_limits.hpp>
#include <hlclient/goldsrc/collision/brush_collision_scene_limits.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace hlclient::goldsrc::movement {

// Game-selected exact advertised model/movetype pairs. No asset open is
// authorized; only a nonplayer BBOX with six zero relative bound coordinates
// can match. Missing fields and nonzero bounds retain ordinary box semantics.
struct ServerPointBoxExclusionRule final {
    std::string model_name;
    std::uint32_t move_type{};
};
inline constexpr std::size_t kMaximumServerPointBoxExclusionRules = 8U;
[[nodiscard]] bool valid_server_point_box_exclusion_rules(
    std::span<const ServerPointBoxExclusionRule> rules) noexcept;

struct LocalMovementCollisionQueryConfig {
    hlclient::collision::CollisionQueryLimits query_limits{};
    hlclient::collision::CollisionTraceToleranceProfile trace_tolerance{};
    hlclient::goldsrc::collision::BrushCollisionSceneQueryLimits scene_limits{};
};

[[nodiscard]] bool valid_local_movement_collision_query_config(
    const LocalMovementCollisionQueryConfig& config) noexcept;

} // namespace hlclient::goldsrc::movement
