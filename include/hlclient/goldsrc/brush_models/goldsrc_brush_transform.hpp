#pragma once

#include <hlclient/goldsrc/brush_models/goldsrc_brush_rigid_transform.hpp>
#include <hlclient/renderer/render_camera_math.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::brush_models {

struct BrushSubmodelTransform {
    assets::AssetVector3 translation{};
    // Exact GoldSrc entity order: x=pitch, y=yaw, z=roll, in degrees.
    assets::AssetVector3 rotation_degrees{};
    assets::AssetVector3 source_model_origin{};
    renderer::RenderMatrix4 model_matrix{};
    renderer::RenderMatrix4 inverse_model_matrix{};
    // Column-major rotation-only normal matrix. No scale is supported.
    std::array<float, 9U> normal_matrix{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
    };
    BrushSubmodelCoordinateProfile coordinate_profile{
        BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1};
    BrushSubmodelTransformProfile transform_profile{
        BrushSubmodelTransformProfile::valve_angle_matrix_entity_origin_v1};
};

struct BrushSubmodelTransformResult {
    std::optional<BrushSubmodelTransform> transform;
    std::optional<BrushSubmodelTransformError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return transform.has_value();
    }
};

// The pinned compiler emits the supported profile with dmodel_t::origin at
// zero; it uses the entity origin as the local-space pivot/translation. A
// nonzero source-model origin is therefore rejected instead of guessing an
// unproven second pivot formula.
[[nodiscard]] BrushSubmodelTransformResult make_brush_submodel_transform(
    const assets::AssetVector3& entity_origin,
    const assets::AssetVector3& entity_angles_degrees,
    const assets::AssetVector3& source_model_origin = {}) noexcept;

[[nodiscard]] assets::AssetVector3 transform_brush_point(
    const BrushSubmodelTransform& transform,
    const assets::AssetVector3& point) noexcept;

[[nodiscard]] assets::AssetVector3 transform_brush_normal(
    const BrushSubmodelTransform& transform,
    const assets::AssetVector3& normal) noexcept;

struct BrushSubmodelBoundsResult {
    std::optional<assets::WorldBounds> bounds;
    std::optional<BrushSubmodelTransformError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return bounds.has_value();
    }
};

// Transforms all eight AABB corners and recomputes an axis-aligned world bound.
[[nodiscard]] BrushSubmodelBoundsResult transform_brush_bounds(
    const assets::WorldBounds& local_bounds,
    const BrushSubmodelTransform& transform) noexcept;

} // namespace hlclient::goldsrc::brush_models
