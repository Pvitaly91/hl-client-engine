#include <hlclient/goldsrc/brush_models/goldsrc_brush_rigid_transform.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::goldsrc::brush_models {
namespace {

constexpr float kDegreesToRadians = 0.01745329251994329577F;
constexpr float kOrthonormalTolerance = 1.0e-4F;

[[nodiscard]] BrushRigidTransformResult fail_transform(
    const BrushSubmodelTransformErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, BrushSubmodelTransformError{code, context}};
}

[[nodiscard]] BrushRigidBoundsResult fail_bounds(
    const BrushSubmodelTransformErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, BrushSubmodelTransformError{code, context}};
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool zero_vector(const assets::AssetVector3& value) noexcept
{
    return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

[[nodiscard]] bool finite_basis(const BrushRigidRotationBasis& basis) noexcept
{
    return finite_vector(basis.local_x_in_world) &&
        finite_vector(basis.local_y_in_world) &&
        finite_vector(basis.local_z_in_world);
}

[[nodiscard]] bool make_valve_rotation_basis(
    const assets::AssetVector3& angles_degrees,
    BrushRigidRotationBasis& basis) noexcept
{
    const auto pitch = angles_degrees.x * kDegreesToRadians;
    const auto yaw = angles_degrees.y * kDegreesToRadians;
    const auto roll = angles_degrees.z * kDegreesToRadians;
    if (!std::isfinite(pitch) || !std::isfinite(yaw) ||
        !std::isfinite(roll)) {
        return false;
    }

    const auto sine_pitch = std::sin(pitch);
    const auto cosine_pitch = std::cos(pitch);
    const auto sine_yaw = std::sin(yaw);
    const auto cosine_yaw = std::cos(yaw);
    const auto sine_roll = std::sin(roll);
    const auto cosine_roll = std::cos(roll);

    basis = {
        {
            cosine_pitch * cosine_yaw,
            cosine_pitch * sine_yaw,
            -sine_pitch,
        },
        {
            sine_roll * sine_pitch * cosine_yaw - cosine_roll * sine_yaw,
            sine_roll * sine_pitch * sine_yaw + cosine_roll * cosine_yaw,
            sine_roll * cosine_pitch,
        },
        {
            cosine_roll * sine_pitch * cosine_yaw + sine_roll * sine_yaw,
            cosine_roll * sine_pitch * sine_yaw - sine_roll * cosine_yaw,
            cosine_roll * cosine_pitch,
        },
    };
    return finite_basis(basis);
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] float dot(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] assets::AssetVector3 cross(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] bool approximately(
    const float value,
    const float expected) noexcept
{
    return std::isfinite(value) &&
        std::abs(value - expected) <= kOrthonormalTolerance;
}

[[nodiscard]] bool approximately(
    const assets::AssetVector3& value,
    const assets::AssetVector3& expected) noexcept
{
    return approximately(value.x, expected.x) &&
        approximately(value.y, expected.y) &&
        approximately(value.z, expected.z);
}

[[nodiscard]] assets::AssetVector3 subtract(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
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

BrushRigidTransformResult make_brush_rigid_transform(
    const assets::AssetVector3& entity_origin,
    const assets::AssetVector3& entity_angles_degrees,
    const assets::AssetVector3& source_model_origin) noexcept
{
    if (!finite_vector(entity_origin) ||
        !finite_vector(entity_angles_degrees) ||
        !finite_vector(source_model_origin)) {
        return fail_transform(BrushSubmodelTransformErrorCode::non_finite_input,
            "Brush origin, angles and source-model origin must be finite");
    }
    if (!zero_vector(source_model_origin)) {
        return fail_transform(
            BrushSubmodelTransformErrorCode::unsupported_source_model_origin,
            "Pinned qcsg evidence supports only zero dmodel origin");
    }

    BrushRigidRotationBasis basis{};
    if (!make_valve_rotation_basis(entity_angles_degrees, basis)) {
        return fail_transform(BrushSubmodelTransformErrorCode::non_finite_result,
            "Valve-profile model matrix is not finite");
    }

    return {
        BrushRigidTransform{
            entity_origin,
            basis,
            entity_angles_degrees,
            source_model_origin,
            BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1,
            BrushSubmodelTransformProfile::valve_angle_matrix_entity_origin_v1,
        },
        std::nullopt,
    };
}

bool valid_brush_rigid_transform(
    const BrushRigidTransform& transform) noexcept
{
    if (!finite_vector(transform.translation) ||
        !finite_vector(transform.rotation_degrees) ||
        !finite_vector(transform.source_model_origin) ||
        !zero_vector(transform.source_model_origin) ||
        !finite_basis(transform.rotation_basis) ||
        transform.coordinate_profile !=
            BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1 ||
        transform.transform_profile !=
            BrushSubmodelTransformProfile::
                valve_angle_matrix_entity_origin_v1) {
        return false;
    }

    BrushRigidRotationBasis expected_basis{};
    if (!make_valve_rotation_basis(
            transform.rotation_degrees, expected_basis) ||
        !approximately(transform.rotation_basis.local_x_in_world,
            expected_basis.local_x_in_world) ||
        !approximately(transform.rotation_basis.local_y_in_world,
            expected_basis.local_y_in_world) ||
        !approximately(transform.rotation_basis.local_z_in_world,
            expected_basis.local_z_in_world)) {
        return false;
    }

    const auto& x = transform.rotation_basis.local_x_in_world;
    const auto& y = transform.rotation_basis.local_y_in_world;
    const auto& z = transform.rotation_basis.local_z_in_world;
    const auto handed_z = cross(x, y);
    return approximately(dot(x, x), 1.0F) &&
        approximately(dot(y, y), 1.0F) &&
        approximately(dot(z, z), 1.0F) &&
        approximately(dot(x, y), 0.0F) &&
        approximately(dot(x, z), 0.0F) &&
        approximately(dot(y, z), 0.0F) &&
        approximately(handed_z.x, z.x) &&
        approximately(handed_z.y, z.y) &&
        approximately(handed_z.z, z.z);
}

assets::AssetVector3 brush_rigid_local_to_world_vector(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& vector) noexcept
{
    const auto& basis = transform.rotation_basis;
    return {
        basis.local_x_in_world.x * vector.x +
            basis.local_y_in_world.x * vector.y +
            basis.local_z_in_world.x * vector.z,
        basis.local_x_in_world.y * vector.x +
            basis.local_y_in_world.y * vector.y +
            basis.local_z_in_world.y * vector.z,
        basis.local_x_in_world.z * vector.x +
            basis.local_y_in_world.z * vector.y +
            basis.local_z_in_world.z * vector.z,
    };
}

assets::AssetVector3 brush_rigid_world_to_local_vector(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& vector) noexcept
{
    const auto& basis = transform.rotation_basis;
    return {
        dot(vector, basis.local_x_in_world),
        dot(vector, basis.local_y_in_world),
        dot(vector, basis.local_z_in_world),
    };
}

assets::AssetVector3 brush_rigid_local_to_world_point(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& point) noexcept
{
    const auto rotated = brush_rigid_local_to_world_vector(transform, point);
    return {
        rotated.x + transform.translation.x,
        rotated.y + transform.translation.y,
        rotated.z + transform.translation.z,
    };
}

assets::AssetVector3 brush_rigid_world_to_local_point(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& point) noexcept
{
    return brush_rigid_world_to_local_vector(
        transform, subtract(point, transform.translation));
}

assets::AssetVector3 brush_rigid_local_to_world_normal(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& normal) noexcept
{
    return brush_rigid_local_to_world_vector(transform, normal);
}

assets::AssetVector3 brush_rigid_world_to_local_normal(
    const BrushRigidTransform& transform,
    const assets::AssetVector3& normal) noexcept
{
    return brush_rigid_world_to_local_vector(transform, normal);
}

BrushRigidBoundsResult transform_brush_rigid_bounds(
    const assets::WorldBounds& local_bounds,
    const BrushRigidTransform& transform) noexcept
{
    if (!valid_bounds(local_bounds)) {
        return fail_bounds(
            BrushSubmodelTransformErrorCode::invalid_local_bounds,
            "Local brush bounds must be finite and ordered");
    }
    if (!valid_brush_rigid_transform(transform)) {
        return fail_bounds(BrushSubmodelTransformErrorCode::non_finite_input,
            "Brush rigid transform must be valid and self-consistent");
    }

    assets::WorldBounds transformed_bounds{};
    bool first = true;
    for (std::size_t corner = 0U; corner < 8U; ++corner) {
        const assets::AssetVector3 local_point{
            (corner & 1U) == 0U ? local_bounds.minimum.x : local_bounds.maximum.x,
            (corner & 2U) == 0U ? local_bounds.minimum.y : local_bounds.maximum.y,
            (corner & 4U) == 0U ? local_bounds.minimum.z : local_bounds.maximum.z,
        };
        const auto world_point =
            brush_rigid_local_to_world_point(transform, local_point);
        if (!finite_vector(world_point)) {
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
