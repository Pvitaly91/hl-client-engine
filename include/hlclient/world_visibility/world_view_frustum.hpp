#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/renderer/render_camera_math.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::world_visibility {

// Plane equations use dot(normal, point) + signed_offset >= 0 for the
// interior half-space. The normal and offset are normalized together.
struct WorldFrustumPlane {
    assets::AssetVector3 normal{};
    float signed_offset{0.0F};

    [[nodiscard]] friend bool operator==(
        const WorldFrustumPlane& left,
        const WorldFrustumPlane& right) noexcept
    {
        return left.normal.x == right.normal.x &&
            left.normal.y == right.normal.y &&
            left.normal.z == right.normal.z &&
            left.signed_offset == right.signed_offset;
    }
};

enum class WorldFrustumPlaneIndex : std::size_t {
    left = 0U,
    right,
    bottom,
    top,
    near,
    far,
};

enum class WorldBoundsClassification {
    outside,
    intersecting,
    inside,
};

enum class WorldViewFrustumErrorCode {
    invalid_camera,
    invalid_extent,
    non_finite_matrix,
    degenerate_plane,
    non_finite_plane,
    invalid_bounds,
};

[[nodiscard]] std::string_view to_string(
    WorldViewFrustumErrorCode code) noexcept;

class WorldViewFrustum;
struct WorldViewFrustumCreateResult;

struct WorldBoundsClassificationResult {
    std::optional<WorldBoundsClassification> classification;
    std::optional<WorldViewFrustumErrorCode> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return classification.has_value();
    }
};

class WorldViewFrustum final {
public:
    static constexpr std::size_t plane_count = 6U;

    [[nodiscard]] static WorldViewFrustumCreateResult from_view_projection(
        const renderer::RenderMatrix4& view_projection) noexcept;

    [[nodiscard]] static WorldViewFrustumCreateResult from_camera(
        const renderer::RenderCamera& camera,
        renderer::RenderExtent extent) noexcept;

    [[nodiscard]] std::span<const WorldFrustumPlane, plane_count> planes()
        const noexcept;

    [[nodiscard]] const WorldFrustumPlane& plane(
        WorldFrustumPlaneIndex index) const noexcept;

    [[nodiscard]] WorldBoundsClassificationResult classify(
        const assets::WorldBounds& bounds) const noexcept;

private:
    explicit WorldViewFrustum(
        std::array<WorldFrustumPlane, plane_count> planes) noexcept;

    std::array<WorldFrustumPlane, plane_count> planes_{};
};

struct WorldViewFrustumCreateResult {
    std::optional<WorldViewFrustum> frustum;
    std::optional<WorldViewFrustumErrorCode> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return frustum.has_value();
    }
};

} // namespace hlclient::world_visibility
