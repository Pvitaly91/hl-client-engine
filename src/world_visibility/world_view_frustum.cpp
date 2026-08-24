#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace hlclient::world_visibility {
namespace {

constexpr float kMinimumPlaneNormalLength = 1.0e-7F;

[[nodiscard]] bool finite(const assets::WorldBounds& bounds) noexcept
{
    return renderer::is_finite(bounds.minimum) &&
        renderer::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] WorldViewFrustumCreateResult fail_create(
    const WorldViewFrustumErrorCode code) noexcept
{
    return {std::nullopt, code};
}

[[nodiscard]] WorldBoundsClassificationResult fail_classification(
    const WorldViewFrustumErrorCode code) noexcept
{
    return {std::nullopt, code};
}

[[nodiscard]] std::array<float, 4U> matrix_row(
    const renderer::RenderMatrix4& matrix,
    const std::size_t row) noexcept
{
    return {
        matrix.values[row],
        matrix.values[4U + row],
        matrix.values[8U + row],
        matrix.values[12U + row],
    };
}

[[nodiscard]] std::array<float, 4U> add(
    const std::array<float, 4U>& left,
    const std::array<float, 4U>& right) noexcept
{
    return {
        left[0U] + right[0U],
        left[1U] + right[1U],
        left[2U] + right[2U],
        left[3U] + right[3U],
    };
}

[[nodiscard]] std::array<float, 4U> subtract(
    const std::array<float, 4U>& left,
    const std::array<float, 4U>& right) noexcept
{
    return {
        left[0U] - right[0U],
        left[1U] - right[1U],
        left[2U] - right[2U],
        left[3U] - right[3U],
    };
}

[[nodiscard]] std::optional<WorldFrustumPlane> normalize_plane(
    const std::array<float, 4U>& coefficients,
    WorldViewFrustumErrorCode& error) noexcept
{
    if (!std::ranges::all_of(coefficients, [](const float value) {
            return std::isfinite(value);
        })) {
        error = WorldViewFrustumErrorCode::non_finite_plane;
        return std::nullopt;
    }

    const double squared_length =
        static_cast<double>(coefficients[0U]) * coefficients[0U] +
        static_cast<double>(coefficients[1U]) * coefficients[1U] +
        static_cast<double>(coefficients[2U]) * coefficients[2U];
    if (!std::isfinite(squared_length) ||
        squared_length <= static_cast<double>(kMinimumPlaneNormalLength) *
                kMinimumPlaneNormalLength) {
        error = WorldViewFrustumErrorCode::degenerate_plane;
        return std::nullopt;
    }

    const double inverse_length = 1.0 / std::sqrt(squared_length);
    WorldFrustumPlane plane{
        {
            static_cast<float>(coefficients[0U] * inverse_length),
            static_cast<float>(coefficients[1U] * inverse_length),
            static_cast<float>(coefficients[2U] * inverse_length),
        },
        static_cast<float>(coefficients[3U] * inverse_length),
    };
    if (!renderer::is_finite(plane.normal) ||
        !std::isfinite(plane.signed_offset)) {
        error = WorldViewFrustumErrorCode::non_finite_plane;
        return std::nullopt;
    }
    return plane;
}

[[nodiscard]] float signed_distance(
    const WorldFrustumPlane& plane,
    const assets::AssetVector3& point) noexcept
{
    return renderer::dot(plane.normal, point) + plane.signed_offset;
}

} // namespace

std::string_view to_string(const WorldViewFrustumErrorCode code) noexcept
{
    switch (code) {
    case WorldViewFrustumErrorCode::invalid_camera:
        return "invalid_camera";
    case WorldViewFrustumErrorCode::invalid_extent:
        return "invalid_extent";
    case WorldViewFrustumErrorCode::non_finite_matrix:
        return "non_finite_matrix";
    case WorldViewFrustumErrorCode::degenerate_plane:
        return "degenerate_plane";
    case WorldViewFrustumErrorCode::non_finite_plane:
        return "non_finite_plane";
    case WorldViewFrustumErrorCode::invalid_bounds:
        return "invalid_bounds";
    }
    return "unknown";
}

WorldViewFrustum::WorldViewFrustum(
    std::array<WorldFrustumPlane, plane_count> planes) noexcept
    : planes_{std::move(planes)}
{
}

WorldViewFrustumCreateResult WorldViewFrustum::from_view_projection(
    const renderer::RenderMatrix4& view_projection) noexcept
{
    if (!renderer::is_finite(view_projection)) {
        return fail_create(WorldViewFrustumErrorCode::non_finite_matrix);
    }

    // Clip inequalities are -w <= x/y/z <= w. With column vectors and a
    // column-major stored matrix, the world-space half-spaces are row3 +/-
    // row0/1/2. The order is part of the public profile.
    const auto row0 = matrix_row(view_projection, 0U);
    const auto row1 = matrix_row(view_projection, 1U);
    const auto row2 = matrix_row(view_projection, 2U);
    const auto row3 = matrix_row(view_projection, 3U);
    const std::array<std::array<float, 4U>, plane_count> coefficients{
        add(row3, row0),
        subtract(row3, row0),
        add(row3, row1),
        subtract(row3, row1),
        add(row3, row2),
        subtract(row3, row2),
    };

    std::array<WorldFrustumPlane, plane_count> planes{};
    for (std::size_t index = 0U; index < coefficients.size(); ++index) {
        auto error = WorldViewFrustumErrorCode::degenerate_plane;
        auto plane = normalize_plane(coefficients[index], error);
        if (!plane) {
            return fail_create(error);
        }
        planes[index] = *plane;
    }
    return {WorldViewFrustum{std::move(planes)}, std::nullopt};
}

WorldViewFrustumCreateResult WorldViewFrustum::from_camera(
    const renderer::RenderCamera& camera,
    const renderer::RenderExtent extent) noexcept
{
    if (!renderer::is_valid(camera)) {
        return fail_create(WorldViewFrustumErrorCode::invalid_camera);
    }
    if (extent.width <= 0 || extent.height <= 0) {
        return fail_create(WorldViewFrustumErrorCode::invalid_extent);
    }
    const auto view_projection = renderer::camera_view_projection(camera, extent);
    if (!view_projection || !view_projection.matrix) {
        return fail_create(WorldViewFrustumErrorCode::invalid_camera);
    }
    return from_view_projection(*view_projection.matrix);
}

std::span<const WorldFrustumPlane, WorldViewFrustum::plane_count>
WorldViewFrustum::planes() const noexcept
{
    return planes_;
}

const WorldFrustumPlane& WorldViewFrustum::plane(
    const WorldFrustumPlaneIndex index) const noexcept
{
    return planes_[static_cast<std::size_t>(index)];
}

WorldBoundsClassificationResult WorldViewFrustum::classify(
    const assets::WorldBounds& bounds) const noexcept
{
    if (!finite(bounds)) {
        return fail_classification(WorldViewFrustumErrorCode::invalid_bounds);
    }

    auto classification = WorldBoundsClassification::inside;
    for (const auto& current : planes_) {
        const assets::AssetVector3 positive{
            current.normal.x >= 0.0F ? bounds.maximum.x : bounds.minimum.x,
            current.normal.y >= 0.0F ? bounds.maximum.y : bounds.minimum.y,
            current.normal.z >= 0.0F ? bounds.maximum.z : bounds.minimum.z,
        };
        if (signed_distance(current, positive) < 0.0F) {
            return {WorldBoundsClassification::outside, std::nullopt};
        }

        const assets::AssetVector3 negative{
            current.normal.x >= 0.0F ? bounds.minimum.x : bounds.maximum.x,
            current.normal.y >= 0.0F ? bounds.minimum.y : bounds.maximum.y,
            current.normal.z >= 0.0F ? bounds.minimum.z : bounds.maximum.z,
        };
        if (signed_distance(current, negative) < 0.0F) {
            classification = WorldBoundsClassification::intersecting;
        }
    }
    return {classification, std::nullopt};
}

} // namespace hlclient::world_visibility
