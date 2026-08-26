#include <hlclient/entity_render/entity_render_types.hpp>

#include <cmath>

namespace hlclient::entity_render {

namespace {

[[nodiscard]] bool nonzero_at_most(
    const std::size_t value,
    const std::size_t maximum) noexcept
{
    return value > 0U && value <= maximum;
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

} // namespace

bool valid_runtime_entity_visual_limits(
    const RuntimeEntityVisualLimits& limits) noexcept
{
    const auto& hard = kRuntimeEntityVisualHardLimits;
    return nonzero_at_most(limits.maximum_entities, hard.maximum_entities) &&
        nonzero_at_most(
            limits.maximum_visual_assets, hard.maximum_visual_assets) &&
        nonzero_at_most(
            limits.maximum_studio_instances, hard.maximum_studio_instances) &&
        nonzero_at_most(
            limits.maximum_sprite_instances, hard.maximum_sprite_instances) &&
        nonzero_at_most(limits.maximum_pose_count, hard.maximum_pose_count) &&
        nonzero_at_most(
            limits.maximum_total_bone_matrices,
            hard.maximum_total_bone_matrices) &&
        nonzero_at_most(
            limits.maximum_model_gpu_bytes, hard.maximum_model_gpu_bytes) &&
        nonzero_at_most(
            limits.maximum_sprite_gpu_bytes, hard.maximum_sprite_gpu_bytes) &&
        nonzero_at_most(
            limits.maximum_entity_draws, hard.maximum_entity_draws) &&
        nonzero_at_most(
            limits.maximum_entity_events, hard.maximum_entity_events) &&
        nonzero_at_most(
            limits.maximum_imports_per_update,
            hard.maximum_imports_per_update) &&
        nonzero_at_most(
            limits.maximum_pose_evaluations_per_update,
            hard.maximum_pose_evaluations_per_update) &&
        limits.maximum_studio_instances <= limits.maximum_entities &&
        limits.maximum_sprite_instances <= limits.maximum_entities &&
        limits.maximum_pose_count <= limits.maximum_entities &&
        limits.maximum_pose_evaluations_per_update <= limits.maximum_pose_count;
}

bool finite_entity_render_transform(
    const EntityRenderTransform& transform) noexcept
{
    return finite_vector(transform.origin) &&
        finite_vector(transform.rotation_degrees) &&
        std::isfinite(transform.uniform_scale) &&
        transform.uniform_scale > 0.0F;
}

bool finite_entity_render_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

} // namespace hlclient::entity_render
