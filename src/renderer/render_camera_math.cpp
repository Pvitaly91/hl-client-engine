#include <hlclient/renderer/render_camera_math.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hlclient::renderer {
namespace {

constexpr float kVectorEpsilon = 1.0e-6F;
constexpr float kMinimumFieldOfViewRadians = 0.0174532925F;
constexpr float kMaximumFieldOfViewRadians = 3.1241393611F;

[[nodiscard]] assets::AssetVector3 subtract(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] assets::AssetVector3 scale(
    const assets::AssetVector3& value,
    const float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] RenderMatrixResult fail(const RenderCameraMathErrorCode code) noexcept
{
    return {std::nullopt, code};
}

} // namespace

std::string_view to_string(const RenderCameraMathErrorCode code) noexcept
{
    switch (code) {
    case RenderCameraMathErrorCode::non_finite_input:
        return "non_finite_input";
    case RenderCameraMathErrorCode::zero_forward:
        return "zero_forward";
    case RenderCameraMathErrorCode::zero_up:
        return "zero_up";
    case RenderCameraMathErrorCode::parallel_forward_and_up:
        return "parallel_forward_and_up";
    case RenderCameraMathErrorCode::invalid_field_of_view:
        return "invalid_field_of_view";
    case RenderCameraMathErrorCode::invalid_aspect_ratio:
        return "invalid_aspect_ratio";
    case RenderCameraMathErrorCode::invalid_depth_range:
        return "invalid_depth_range";
    case RenderCameraMathErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

bool is_finite(const assets::AssetVector2& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool is_finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool is_finite(const RenderMatrix4& value) noexcept
{
    return std::all_of(value.values.begin(), value.values.end(), [](const float element) {
        return std::isfinite(element);
    });
}

float dot(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

assets::AssetVector3 cross(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

std::optional<assets::AssetVector3> normalize(
    const assets::AssetVector3& value) noexcept
{
    if (!is_finite(value)) {
        return std::nullopt;
    }
    const auto squared_length = dot(value, value);
    if (!std::isfinite(squared_length) || squared_length <= kVectorEpsilon * kVectorEpsilon) {
        return std::nullopt;
    }
    const auto inverse_length = 1.0F / std::sqrt(squared_length);
    const auto result = scale(value, inverse_length);
    return is_finite(result) ? std::optional{result} : std::nullopt;
}

RenderMatrix4 multiply(
    const RenderMatrix4& left,
    const RenderMatrix4& right) noexcept
{
    RenderMatrix4 result;
    result.values.fill(0.0F);
    for (std::size_t column = 0U; column < 4U; ++column) {
        for (std::size_t row = 0U; row < 4U; ++row) {
            for (std::size_t inner = 0U; inner < 4U; ++inner) {
                result.values[column * 4U + row] +=
                    left.values[inner * 4U + row] *
                    right.values[column * 4U + inner];
            }
        }
    }
    return result;
}

RenderHomogeneousVector transform(
    const RenderMatrix4& matrix,
    const RenderHomogeneousVector& value) noexcept
{
    return {
        matrix.values[0U] * value.x + matrix.values[4U] * value.y +
            matrix.values[8U] * value.z + matrix.values[12U] * value.w,
        matrix.values[1U] * value.x + matrix.values[5U] * value.y +
            matrix.values[9U] * value.z + matrix.values[13U] * value.w,
        matrix.values[2U] * value.x + matrix.values[6U] * value.y +
            matrix.values[10U] * value.z + matrix.values[14U] * value.w,
        matrix.values[3U] * value.x + matrix.values[7U] * value.y +
            matrix.values[11U] * value.z + matrix.values[15U] * value.w,
    };
}

RenderMatrixResult right_handed_look_at(
    const assets::AssetVector3& position,
    const assets::AssetVector3& target,
    const assets::AssetVector3& up) noexcept
{
    if (!is_finite(position) || !is_finite(target) || !is_finite(up)) {
        return fail(RenderCameraMathErrorCode::non_finite_input);
    }
    const auto forward = normalize(subtract(target, position));
    if (!forward) {
        return fail(RenderCameraMathErrorCode::zero_forward);
    }
    const auto normalized_up = normalize(up);
    if (!normalized_up) {
        return fail(RenderCameraMathErrorCode::zero_up);
    }
    const auto side = normalize(cross(*forward, *normalized_up));
    if (!side) {
        return fail(RenderCameraMathErrorCode::parallel_forward_and_up);
    }
    const auto camera_up = cross(*side, *forward);

    RenderMatrix4 result;
    result.values = {
        side->x, camera_up.x, -forward->x, 0.0F,
        side->y, camera_up.y, -forward->y, 0.0F,
        side->z, camera_up.z, -forward->z, 0.0F,
        -dot(*side, position), -dot(camera_up, position), dot(*forward, position), 1.0F,
    };
    if (!is_finite(result)) {
        return fail(RenderCameraMathErrorCode::non_finite_result);
    }
    return {result, std::nullopt};
}

RenderMatrixResult opengl_perspective(
    const float vertical_field_of_view_radians,
    const float aspect_ratio,
    const float near_plane,
    const float far_plane) noexcept
{
    if (!std::isfinite(vertical_field_of_view_radians) ||
        !std::isfinite(aspect_ratio) || !std::isfinite(near_plane) ||
        !std::isfinite(far_plane)) {
        return fail(RenderCameraMathErrorCode::non_finite_input);
    }
    if (vertical_field_of_view_radians <= kMinimumFieldOfViewRadians ||
        vertical_field_of_view_radians >= kMaximumFieldOfViewRadians) {
        return fail(RenderCameraMathErrorCode::invalid_field_of_view);
    }
    if (aspect_ratio <= 0.0F) {
        return fail(RenderCameraMathErrorCode::invalid_aspect_ratio);
    }
    if (near_plane <= 0.0F || far_plane <= near_plane) {
        return fail(RenderCameraMathErrorCode::invalid_depth_range);
    }

    const auto tangent = std::tan(vertical_field_of_view_radians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return fail(RenderCameraMathErrorCode::invalid_field_of_view);
    }
    const auto focal_length = 1.0F / tangent;
    RenderMatrix4 result;
    result.values.fill(0.0F);
    result.values[0U] = focal_length / aspect_ratio;
    result.values[5U] = focal_length;
    result.values[10U] = (far_plane + near_plane) / (near_plane - far_plane);
    result.values[11U] = -1.0F;
    result.values[14U] =
        (2.0F * far_plane * near_plane) / (near_plane - far_plane);
    if (!is_finite(result)) {
        return fail(RenderCameraMathErrorCode::non_finite_result);
    }
    return {result, std::nullopt};
}

bool is_valid(const RenderCamera& camera) noexcept
{
    if (!right_handed_look_at(camera.position, camera.target, camera.up)) {
        return false;
    }
    return static_cast<bool>(opengl_perspective(
        camera.vertical_field_of_view_radians,
        1.0F,
        camera.near_plane,
        camera.far_plane));
}

RenderMatrixResult camera_view_projection(
    const RenderCamera& camera,
    const RenderExtent extent) noexcept
{
    if (extent.width <= 0 || extent.height <= 0) {
        return fail(RenderCameraMathErrorCode::invalid_aspect_ratio);
    }
    const auto view = right_handed_look_at(camera.position, camera.target, camera.up);
    if (!view) {
        return view;
    }
    const auto aspect = static_cast<float>(extent.width) /
        static_cast<float>(extent.height);
    const auto projection = opengl_perspective(
        camera.vertical_field_of_view_radians,
        aspect,
        camera.near_plane,
        camera.far_plane);
    if (!projection) {
        return projection;
    }
    const auto result = multiply(*projection.matrix, *view.matrix);
    if (!is_finite(result)) {
        return fail(RenderCameraMathErrorCode::non_finite_result);
    }
    return {result, std::nullopt};
}

} // namespace hlclient::renderer
