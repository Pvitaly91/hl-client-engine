#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/renderer/render_scene.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace hlclient::renderer {

struct RenderHomogeneousVector {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{0.0F};
};

enum class RenderCameraMathErrorCode {
    non_finite_input,
    zero_forward,
    zero_up,
    parallel_forward_and_up,
    invalid_field_of_view,
    invalid_aspect_ratio,
    invalid_depth_range,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(RenderCameraMathErrorCode code) noexcept;

struct RenderMatrixResult {
    std::optional<RenderMatrix4> matrix;
    std::optional<RenderCameraMathErrorCode> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return matrix.has_value();
    }
};

[[nodiscard]] bool is_finite(const assets::AssetVector2& value) noexcept;
[[nodiscard]] bool is_finite(const assets::AssetVector3& value) noexcept;
[[nodiscard]] bool is_finite(const RenderMatrix4& value) noexcept;
[[nodiscard]] float dot(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept;
[[nodiscard]] assets::AssetVector3 cross(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept;
[[nodiscard]] std::optional<assets::AssetVector3> normalize(
    const assets::AssetVector3& value) noexcept;
[[nodiscard]] RenderMatrix4 multiply(
    const RenderMatrix4& left,
    const RenderMatrix4& right) noexcept;
[[nodiscard]] RenderHomogeneousVector transform(
    const RenderMatrix4& matrix,
    const RenderHomogeneousVector& value) noexcept;
[[nodiscard]] RenderMatrixResult right_handed_look_at(
    const assets::AssetVector3& position,
    const assets::AssetVector3& target,
    const assets::AssetVector3& up) noexcept;
[[nodiscard]] RenderMatrixResult opengl_perspective(
    float vertical_field_of_view_radians,
    float aspect_ratio,
    float near_plane,
    float far_plane) noexcept;
[[nodiscard]] bool is_valid(const RenderCamera& camera) noexcept;
[[nodiscard]] bool valid_render_screen_effects(const game_api::ScreenEffectsFrame&) noexcept;
// Applies only to a private render-camera value, preserving its target axis,
// projection and caller ownership. Invalid effects/cameras have no fallback.
[[nodiscard]] std::optional<RenderCamera> apply_screen_effects(
    const RenderCamera&, const game_api::ScreenEffectsFrame&) noexcept;
[[nodiscard]] RenderMatrixResult camera_view_projection(
    const RenderCamera& camera,
    RenderExtent extent) noexcept;
// Uses the world camera's FOV/far plane but a fixed +X-forward, Z-up camera
// at the origin and an independent 1-unit near plane. First-person Studio
// geometry stays camera-local; the world camera/depth range is not mutated.
[[nodiscard]] RenderCamera camera_local_first_person_camera(
    const RenderCamera& world_camera) noexcept;

} // namespace hlclient::renderer
