#include <hlclient/gameplay_camera/render_camera_adapter.hpp>

#include <hlclient/renderer/render_camera_math.hpp>

#include <cmath>

namespace hlclient::gameplay_camera {
namespace {

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

} // namespace

RenderCameraBuildResult build_render_camera(
    const GameplayCameraState& state) noexcept
{
    const auto forward = forward_from_yaw_pitch(
        state.yaw_degrees(), state.pitch_degrees());
    if (!forward || !finite(state.position())) {
        return {std::nullopt,
            GameplayCameraError{GameplayCameraErrorCode::non_finite_camera,
                "camera basis contains a non-finite value"}};
    }
    const auto target_x = static_cast<double>(state.position().x) +
        static_cast<double>(forward->x);
    const auto target_y = static_cast<double>(state.position().y) +
        static_cast<double>(forward->y);
    const auto target_z = static_cast<double>(state.position().z) +
        static_cast<double>(forward->z);
    renderer::RenderCamera camera;
    camera.position = state.position();
    camera.target = {static_cast<float>(target_x),
        static_cast<float>(target_y),
        static_cast<float>(target_z)};
    camera.up = world_up();
    camera.vertical_field_of_view_radians =
        static_cast<float>(state.vertical_fov_radians());
    camera.near_plane = static_cast<float>(state.near_plane());
    camera.far_plane = static_cast<float>(state.far_plane());
    if (!finite(camera.target) || !renderer::is_valid(camera)) {
        return {std::nullopt,
            GameplayCameraError{
                GameplayCameraErrorCode::camera_validation_failed,
                "first-person state failed renderer camera validation"}};
    }
    return {camera, std::nullopt};
}

} // namespace hlclient::gameplay_camera
