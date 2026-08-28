#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <optional>
#include <string_view>

namespace hlclient::goldsrc::brush_models {

enum class BrushSubmodelCoordinateProfile {
    // GoldSrc qcsg removes the origin brush, writes its center to entity
    // `origin`, then subtracts that origin from the remaining brush planes.
    // Pinned evidence: utils/qcsg/map.c:197-225 and
    // utils/qcsg/brush.c:803-820.
    qcsg_entity_origin_relative_v1,
};

enum class BrushSubmodelTransformProfile {
    // Entity angles are [pitch, yaw, roll]. The rotation follows Valve's
    // column-vector AngleMatrix profile: (YAW * PITCH) * ROLL.
    // Pinned evidence: cl_dll/studio_util.cpp:21-48 and
    // utils/qrad/lightmap.c:965-990.
    valve_angle_matrix_entity_origin_v1,
};

enum class BrushSubmodelTransformErrorCode {
    non_finite_input,
    unsupported_source_model_origin,
    invalid_local_bounds,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(
    BrushSubmodelTransformErrorCode code) noexcept;

struct BrushSubmodelTransformError {
    BrushSubmodelTransformErrorCode code{
        BrushSubmodelTransformErrorCode::non_finite_input};
    std::string_view context;
};

// Orthonormal, right-handed rotation basis. Each axis is one local-space
// basis vector expressed in world space. No scale or shear is supported.
struct BrushRigidRotationBasis {
    assets::AssetVector3 local_x_in_world{1.0F, 0.0F, 0.0F};
    assets::AssetVector3 local_y_in_world{0.0F, 1.0F, 0.0F};
    assets::AssetVector3 local_z_in_world{0.0F, 0.0F, 1.0F};
};

// Renderer- and OpenGL-neutral GoldSrc rigid transform. Translation is the
// entity origin. The pinned qcsg profile requires source_model_origin == 0.
struct BrushRigidTransform {
    assets::AssetVector3 translation{};
    BrushRigidRotationBasis rotation_basis{};
    // Exact GoldSrc entity order: x=pitch, y=yaw, z=roll, in degrees.
    assets::AssetVector3 rotation_degrees{};
    assets::AssetVector3 source_model_origin{};
    BrushSubmodelCoordinateProfile coordinate_profile{
        BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1};
    BrushSubmodelTransformProfile transform_profile{
        BrushSubmodelTransformProfile::valve_angle_matrix_entity_origin_v1};
};

struct BrushRigidTransformResult {
    std::optional<BrushRigidTransform> transform;
    std::optional<BrushSubmodelTransformError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return transform.has_value();
    }
};

// The pinned compiler emits the supported profile with dmodel_t::origin at
// zero; it uses the entity origin as the local-space pivot/translation. A
// nonzero source-model origin is rejected rather than applying an unproven
// second pivot formula.
[[nodiscard]] BrushRigidTransformResult make_brush_rigid_transform(
    const assets::AssetVector3& entity_origin,
    const assets::AssetVector3& entity_angles_degrees,
    const assets::AssetVector3& source_model_origin = {}) noexcept;

// Defensive validation for retained/public transforms. Creation always
// satisfies this predicate; consumers can reject manually corrupted values,
// including an orthonormal basis that no longer matches rotation_degrees.
[[nodiscard]] bool valid_brush_rigid_transform(
    const BrushRigidTransform& transform) noexcept;

[[nodiscard]] assets::AssetVector3 brush_rigid_local_to_world_point(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& point) noexcept;

[[nodiscard]] assets::AssetVector3 brush_rigid_world_to_local_point(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& point) noexcept;

[[nodiscard]] assets::AssetVector3 brush_rigid_local_to_world_vector(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& vector) noexcept;

[[nodiscard]] assets::AssetVector3 brush_rigid_world_to_local_vector(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& vector) noexcept;

// Normals use the same rotation as vectors because the transform is rigid and
// has no non-uniform scale.
[[nodiscard]] assets::AssetVector3 brush_rigid_local_to_world_normal(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& normal) noexcept;

[[nodiscard]] assets::AssetVector3 brush_rigid_world_to_local_normal(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& normal) noexcept;

struct BrushRigidBoundsResult {
    std::optional<assets::WorldBounds> bounds;
    std::optional<BrushSubmodelTransformError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return bounds.has_value();
    }
};

// Transforms all eight AABB corners and recomputes a world-space AABB.
[[nodiscard]] BrushRigidBoundsResult transform_brush_rigid_bounds(
    const assets::WorldBounds& local_bounds,
    const BrushRigidTransform& transform) noexcept;

} // namespace hlclient::goldsrc::brush_models
