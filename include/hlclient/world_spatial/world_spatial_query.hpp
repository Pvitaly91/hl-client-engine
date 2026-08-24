#pragma once

#include <hlclient/world_spatial/world_spatial_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hlclient::world_spatial {

inline constexpr std::size_t kWorldSpatialHardMaximumQuerySteps = 65'536U;
inline constexpr std::size_t kWorldSpatialHardMaximumBoxQueryLeaves = 8'192U;
inline constexpr std::size_t kWorldSpatialHardMaximumQueryPlanes = 32'767U;
inline constexpr std::size_t kWorldSpatialHardMaximumQueryNodes = 32'767U;
inline constexpr std::size_t kWorldSpatialHardMaximumQueryPackageLeaves = 8'192U;

struct WorldSpatialQueryLimits {
    std::size_t maximum_query_steps{kWorldSpatialHardMaximumQuerySteps};
    std::size_t maximum_box_query_leaves{
        kWorldSpatialHardMaximumBoxQueryLeaves};
};

[[nodiscard]] bool valid_world_spatial_query_limits(
    const WorldSpatialQueryLimits& limits) noexcept;

enum class WorldSpatialQueryErrorCode {
    invalid_configuration,
    invalid_position,
    invalid_bounds,
    invalid_package,
    invalid_plane_reference,
    invalid_child_reference,
    cycle_detected,
    query_step_limit_exceeded,
    leaf_limit_exceeded,
};

[[nodiscard]] std::string_view to_string(
    WorldSpatialQueryErrorCode code) noexcept;

struct WorldSpatialQueryError {
    WorldSpatialQueryErrorCode code{
        WorldSpatialQueryErrorCode::invalid_configuration};
    std::optional<std::uint32_t> node_index;
    std::size_t traversal_steps{0U};
};

struct WorldPointLeafResult {
    std::uint32_t leaf_index{0U};
    std::int32_t contents{0};
    bool solid_or_special{false};
    bool pvs_available{false};
    std::size_t traversal_depth{0U};
};

struct WorldPointLeafQueryResult {
    std::optional<WorldPointLeafResult> result;
    std::optional<WorldSpatialQueryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value();
    }
};

struct WorldBoxLeavesResult {
    std::vector<std::uint32_t> leaf_indices;
    std::size_t traversal_steps{0U};
};

struct WorldBoxLeavesQueryResult {
    std::optional<WorldBoxLeavesResult> result;
    std::optional<WorldSpatialQueryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value();
    }
};

class WorldSpatialQuery final {
public:
    // Plane-boundary tie policy is exact and deterministic: a value of zero
    // selects child 0 (the front child).
    [[nodiscard]] static WorldPointLeafQueryResult locate_point(
        const WorldSpatialPackage& package,
        assets::AssetVector3 position,
        const WorldSpatialQueryLimits& limits = {});

    [[nodiscard]] static WorldBoxLeavesQueryResult collect_intersecting_leaves(
        const WorldSpatialPackage& package,
        const assets::WorldBounds& bounds,
        const WorldSpatialQueryLimits& limits = {});
};

} // namespace hlclient::world_spatial
