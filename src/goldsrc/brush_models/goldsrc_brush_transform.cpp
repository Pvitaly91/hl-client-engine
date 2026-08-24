#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::goldsrc::brush_models {
namespace {

constexpr float kDegreesToRadians = 0.01745329251994329577F;

[[nodiscard]] BrushSubmodelTransformResult fail_transform(
    const BrushSubmodelTransformErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, BrushSubmodelTransformError{code, context}};
}

[[nodiscard]] BrushSubmodelBoundsResult fail_bounds(
    const BrushSubmodelTransformErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, BrushSubmodelTransformError{code, context}};
}

[[nodiscard]] bool zero_vector(const assets::AssetVector3& value) noexcept
{
    return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return renderer::is_finite(bounds.minimum) &&
        renderer::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

} // namespace

std::string_view to_string(const BrushSubmodelTransformErrorCode code) noexcept
{
    switch (code) {
    case BrushSubmodelTransformErrorCode::non_finite_input:
        return "non_finite_input";
    case BrushSubmodelTransformErrorCode::unsupported_source_model_origin:
        return "unsupported_source_model_origin";
    case BrushSubmodelTransformErrorCode::invalid_local_bounds:
        return "invalid_local_bounds";
    case BrushSubmodelTransformErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

BrushSubmodelTransformResult make_brush_submodel_transform(
    const assets::AssetVector3& entity_origin,
    const assets::AssetVector3& entity_angles_degrees,
    const assets::AssetVector3& source_model_origin) noexcept
{
    if (!renderer::is_finite(entity_origin) ||
        !renderer::is_finite(entity_angles_degrees) ||
        !renderer::is_finite(source_model_origin)) {
        return fail_transform(BrushSubmodelTransformErrorCode::non_finite_input,
            "Brush origin, angles and source-model origin must be finite");
    }
    if (!zero_vector(source_model_origin)) {
        return fail_transform(
            BrushSubmodelTransformErrorCode::unsupported_source_model_origin,
            "Pinned qcsg evidence supports only zero dmodel origin");
    }

    const auto pitch = entity_angles_degrees.x * kDegreesToRadians;
    const auto yaw = entity_angles_degrees.y * kDegreesToRadians;
    const auto roll = entity_angles_degrees.z * kDegreesToRadians;
    if (!std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(roll)) {
        return fail_transform(BrushSubmodelTransformErrorCode::non_finite_result,
            "Degree-to-radian conversion is not finite");
    }

    const auto sine_pitch = std::sin(pitch);
    const auto cosine_pitch = std::cos(pitch);
    const auto sine_yaw = std::sin(yaw);
    const auto cosine_yaw = std::cos(yaw);
    const auto sine_roll = std::sin(roll);
    const auto cosine_roll = std::cos(roll);

    renderer::RenderMatrix4 model_matrix;
    model_matrix.values = {
        cosine_pitch * cosine_yaw,
        cosine_pitch * sine_yaw,
        -sine_pitch,
        0.0F,
        sine_roll * sine_pitch * cosine_yaw - cosine_roll * sine_yaw,
        sine_roll * sine_pitch * sine_yaw + cosine_roll * cosine_yaw,
        sine_roll * cosine_pitch,
        0.0F,
        cosine_roll * sine_pitch * cosine_yaw + sine_roll * sine_yaw,
        cosine_roll * sine_pitch * sine_yaw - sine_roll * cosine_yaw,
        cosine_roll * cosine_pitch,
        0.0F,
        entity_origin.x,
        entity_origin.y,
        entity_origin.z,
        1.0F,
    };
    if (!renderer::is_finite(model_matrix)) {
        return fail_transform(BrushSubmodelTransformErrorCode::non_finite_result,
            "Valve-profile model matrix is not finite");
    }

    renderer::RenderMatrix4 inverse_matrix;
    inverse_matrix.values = {
        model_matrix.values[0U],
        model_matrix.values[4U],
        model_matrix.values[8U],
        0.0F,
        model_matrix.values[1U],
        model_matrix.values[5U],
        model_matrix.values[9U],
        0.0F,
        model_matrix.values[2U],
        model_matrix.values[6U],
        model_matrix.values[10U],
        0.0F,
        -(model_matrix.values[0U] * entity_origin.x +
            model_matrix.values[1U] * entity_origin.y +
            model_matrix.values[2U] * entity_origin.z),
        -(model_matrix.values[4U] * entity_origin.x +
            model_matrix.values[5U] * entity_origin.y +
            model_matrix.values[6U] * entity_origin.z),
        -(model_matrix.values[8U] * entity_origin.x +
            model_matrix.values[9U] * entity_origin.y +
            model_matrix.values[10U] * entity_origin.z),
        1.0F,
    };
    if (!renderer::is_finite(inverse_matrix)) {
        return fail_transform(BrushSubmodelTransformErrorCode::non_finite_result,
            "Inverse brush model matrix is not finite");
    }

    return {
        BrushSubmodelTransform{
            entity_origin,
            entity_angles_degrees,
            source_model_origin,
            model_matrix,
            inverse_matrix,
            {
                model_matrix.values[0U],
                model_matrix.values[1U],
                model_matrix.values[2U],
                model_matrix.values[4U],
                model_matrix.values[5U],
                model_matrix.values[6U],
                model_matrix.values[8U],
                model_matrix.values[9U],
                model_matrix.values[10U],
            },
            BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1,
            BrushSubmodelTransformProfile::valve_angle_matrix_entity_origin_v1,
        },
        std::nullopt,
    };
}

assets::AssetVector3 transform_brush_point(
    const BrushSubmodelTransform& transform,
    const assets::AssetVector3& point) noexcept
{
    const auto transformed = renderer::transform(transform.model_matrix,
        renderer::RenderHomogeneousVector{point.x, point.y, point.z, 1.0F});
    return {transformed.x, transformed.y, transformed.z};
}

assets::AssetVector3 transform_brush_normal(
    const BrushSubmodelTransform& transform,
    const assets::AssetVector3& normal) noexcept
{
    return {
        transform.normal_matrix[0U] * normal.x +
            transform.normal_matrix[3U] * normal.y +
            transform.normal_matrix[6U] * normal.z,
        transform.normal_matrix[1U] * normal.x +
            transform.normal_matrix[4U] * normal.y +
            transform.normal_matrix[7U] * normal.z,
        transform.normal_matrix[2U] * normal.x +
            transform.normal_matrix[5U] * normal.y +
            transform.normal_matrix[8U] * normal.z,
    };
}

BrushSubmodelBoundsResult transform_brush_bounds(
    const assets::WorldBounds& local_bounds,
    const BrushSubmodelTransform& transform) noexcept
{
    if (!valid_bounds(local_bounds)) {
        return fail_bounds(
            BrushSubmodelTransformErrorCode::invalid_local_bounds,
            "Local brush bounds must be finite and ordered");
    }
    if (!renderer::is_finite(transform.model_matrix)) {
        return fail_bounds(BrushSubmodelTransformErrorCode::non_finite_input,
            "Brush model matrix must be finite");
    }

    assets::WorldBounds transformed_bounds{};
    bool first = true;
    for (std::size_t corner = 0U; corner < 8U; ++corner) {
        const assets::AssetVector3 local_point{
            (corner & 1U) == 0U ? local_bounds.minimum.x : local_bounds.maximum.x,
            (corner & 2U) == 0U ? local_bounds.minimum.y : local_bounds.maximum.y,
            (corner & 4U) == 0U ? local_bounds.minimum.z : local_bounds.maximum.z,
        };
        const auto world_point = transform_brush_point(transform, local_point);
        if (!renderer::is_finite(world_point)) {
            return fail_bounds(
                BrushSubmodelTransformErrorCode::non_finite_result,
                "Transformed brush bound corner is not finite");
        }
        if (first) {
            transformed_bounds = {world_point, world_point};
            first = false;
            continue;
        }
        transformed_bounds.minimum.x =
            std::min(transformed_bounds.minimum.x, world_point.x);
        transformed_bounds.minimum.y =
            std::min(transformed_bounds.minimum.y, world_point.y);
        transformed_bounds.minimum.z =
            std::min(transformed_bounds.minimum.z, world_point.z);
        transformed_bounds.maximum.x =
            std::max(transformed_bounds.maximum.x, world_point.x);
        transformed_bounds.maximum.y =
            std::max(transformed_bounds.maximum.y, world_point.y);
        transformed_bounds.maximum.z =
            std::max(transformed_bounds.maximum.z, world_point.z);
    }
    return {transformed_bounds, std::nullopt};
}

} // namespace hlclient::goldsrc::brush_models
