#pragma once

#include <cstddef>

namespace hlclient::goldsrc::collision {

inline constexpr std::size_t kDefaultMaximumBrushCollisionSceneInstances =
    4'096U;
inline constexpr std::size_t kHardMaximumBrushCollisionSceneInstances =
    65'536U;
inline constexpr std::size_t kDefaultMaximumBrushCollisionCandidates = 1'024U;
inline constexpr std::size_t kHardMaximumBrushCollisionCandidates = 65'536U;
inline constexpr std::size_t kDefaultMaximumBrushCollisionModelTraces = 1'024U;
inline constexpr std::size_t kHardMaximumBrushCollisionModelTraces = 65'536U;

struct BrushCollisionSceneBuildLimits {
    std::size_t maximum_instances{
        kDefaultMaximumBrushCollisionSceneInstances};
};

[[nodiscard]] bool valid_brush_collision_scene_build_limits(
    const BrushCollisionSceneBuildLimits& limits) noexcept;

struct BrushCollisionSceneQueryLimits {
    std::size_t maximum_brush_candidates{
        kDefaultMaximumBrushCollisionCandidates};
    std::size_t maximum_model_traces{
        kDefaultMaximumBrushCollisionModelTraces};
};

[[nodiscard]] bool valid_brush_collision_scene_query_limits(
    const BrushCollisionSceneQueryLimits& limits) noexcept;

} // namespace hlclient::goldsrc::collision
