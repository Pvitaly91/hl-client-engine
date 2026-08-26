#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <cstddef>
#include <cstdint>

namespace hlclient::entity_render {

struct EntityRenderResourceIdentity {
    std::uint64_t resource_id{0U};
    std::uint64_t revision{0U};

    [[nodiscard]] friend bool operator==(
        const EntityRenderResourceIdentity&,
        const EntityRenderResourceIdentity&) = default;
};

// Protocol-neutral entity transform. Rotation is expressed explicitly in
// degrees in source-native Z-up coordinates. M4.5.3 permits uniform scale
// only; renderer matrices are derived later at the renderer boundary.
struct EntityRenderTransform {
    assets::AssetVector3 origin{};
    assets::AssetVector3 rotation_degrees{};
    float uniform_scale{1.0F};
};

struct RuntimeEntityVisualLimits {
    std::size_t maximum_entities{4'096U};
    std::size_t maximum_visual_assets{1'024U};
    std::size_t maximum_studio_instances{4'096U};
    std::size_t maximum_sprite_instances{4'096U};
    std::size_t maximum_pose_count{4'096U};
    std::size_t maximum_total_bone_matrices{4'096U * 128U};
    std::size_t maximum_model_gpu_bytes{512U * 1024U * 1024U};
    std::size_t maximum_sprite_gpu_bytes{256U * 1024U * 1024U};
    std::size_t maximum_entity_draws{16'384U};
    std::size_t maximum_entity_events{16'384U};
    std::size_t maximum_imports_per_update{64U};
    std::size_t maximum_pose_evaluations_per_update{4'096U};
};

inline constexpr RuntimeEntityVisualLimits kRuntimeEntityVisualHardLimits{
    65'536U,
    8'192U,
    65'536U,
    65'536U,
    65'536U,
    65'536U * 128U,
    2U * 1024U * 1024U * 1024U,
    1U * 1024U * 1024U * 1024U,
    262'144U,
    262'144U,
    8'192U,
    65'536U,
};

[[nodiscard]] bool valid_runtime_entity_visual_limits(
    const RuntimeEntityVisualLimits& limits) noexcept;

[[nodiscard]] bool finite_entity_render_transform(
    const EntityRenderTransform& transform) noexcept;

[[nodiscard]] bool finite_entity_render_bounds(
    const assets::WorldBounds& bounds) noexcept;

} // namespace hlclient::entity_render
