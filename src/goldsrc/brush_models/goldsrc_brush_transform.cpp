#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::goldsrc::brush_models {
namespace {

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

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return renderer::is_finite(bounds.minimum) &&
        renderer::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

} // namespace

BrushSubmodelTransformResult make_brush_submodel_transform(
    const assets::AssetVector3& entity_origin,
    const assets::AssetVector3& entity_angles_degrees,
    const assets::AssetVector3& source_model_origin) noexcept
{
    const auto rigid = make_brush_rigid_transform(
        entity_origin, entity_angles_degrees, source_model_origin);
    if (!rigid) {
        return fail_transform(rigid.error->code, rigid.error->context);
    }
    const auto& basis = rigid.transform->rotation_basis;

    renderer::RenderMatrix4 model_matrix;
    model_matrix.values = {
        basis.local_x_in_world.x,
        basis.local_x_in_world.y,
        basis.local_x_in_world.z,
        0.0F,
        basis.local_y_in_world.x,
        basis.local_y_in_world.y,
        basis.local_y_in_world.z,
        0.0F,
        basis.local_z_in_world.x,
        basis.local_z_in_world.y,
        basis.local_z_in_world.z,
        0.0F,
        rigid.transform->translation.x,
        rigid.transform->translation.y,
        rigid.transform->translation.z,
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
            rigid.transform->translation,
            rigid.transform->rotation_degrees,
            rigid.transform->source_model_origin,
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
            rigid.transform->coordinate_profile,
            rigid.transform->transform_profile,
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
