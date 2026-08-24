#include <hlclient/world_preview/world_preview_scene_source.hpp>

#include <hlclient/renderer/render_camera_math.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hlclient::world_preview {
namespace {

constexpr float kMinimumNearPlane = 0.1F;
constexpr float kMaximumOrbitAngularVelocity = 1.0F;
constexpr double kFullOrbitRadians = 6.28318530717958647692;

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return renderer::is_finite(bounds.minimum) && renderer::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] assets::AssetVector3 add(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] assets::AssetVector3 scale(
    const assets::AssetVector3& value,
    const float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

} // namespace

WorldPreviewSceneSource::WorldPreviewSceneSource(
    std::shared_ptr<const world_render::WorldRenderPackage> package,
    WorldPreviewSceneOptions options)
    : options_{options}
{
    if (!package) {
        throw std::invalid_argument{"World preview requires an immutable render package"};
    }
    const auto& bounds = package->bounds();
    if (!valid_bounds(bounds)) {
        throw std::invalid_argument{"World preview bounds must be finite and ordered"};
    }
    const auto direction = renderer::normalize(options_.isometric_direction);
    const bool camera_mode_valid =
        options_.camera_mode == WorldPreviewCameraMode::static_camera ||
        options_.camera_mode == WorldPreviewCameraMode::orbit;
    const bool cull_mode_valid =
        options_.cull_mode == client::PreviewWorldCullMode::none ||
        options_.cull_mode == client::PreviewWorldCullMode::back;
    if (!camera_mode_valid || !cull_mode_valid || !direction ||
        !std::isfinite(options_.minimum_radius) ||
        options_.minimum_radius <= 0.0F ||
        !std::isfinite(options_.camera_distance_factor) ||
        options_.camera_distance_factor <= 1.0F ||
        !std::isfinite(options_.orbit_angular_velocity_radians_per_second) ||
        options_.orbit_angular_velocity_radians_per_second < 0.0F ||
        options_.orbit_angular_velocity_radians_per_second >
            kMaximumOrbitAngularVelocity ||
        !std::isfinite(options_.vertical_field_of_view_radians)) {
        throw std::invalid_argument{"World preview camera configuration is invalid"};
    }

    world_center_ = {
        (bounds.minimum.x + bounds.maximum.x) * 0.5F,
        (bounds.minimum.y + bounds.maximum.y) * 0.5F,
        (bounds.minimum.z + bounds.maximum.z) * 0.5F,
    };
    const assets::AssetVector3 half_extent{
        (bounds.maximum.x - bounds.minimum.x) * 0.5F,
        (bounds.maximum.y - bounds.minimum.y) * 0.5F,
        (bounds.maximum.z - bounds.minimum.z) * 0.5F,
    };
    const auto diagonal_radius = std::sqrt(renderer::dot(half_extent, half_extent));
    if (!renderer::is_finite(world_center_) || !std::isfinite(diagonal_radius)) {
        throw std::invalid_argument{"World preview derived bounds are not finite"};
    }
    world_radius_ = std::max(diagonal_radius, options_.minimum_radius);
    base_direction_ = *direction;
    world_state_.set_static_world(std::move(package));
    if (!world_state_.set_preview_render_options(
            client::PreviewRenderOptions{options_.cull_mode})) {
        throw std::invalid_argument{"World preview culling mode is invalid"};
    }
    if (!update_camera()) {
        throw std::invalid_argument{"World preview derived camera is invalid"};
    }
}

client::SceneUpdateResult WorldPreviewSceneSource::update(const client::FrameTime elapsed)
{
    if (!std::isfinite(elapsed.count()) ||
        (elapsed.count() > 0.0 &&
            elapsed.count() >
                std::numeric_limits<double>::max() - world_state_.elapsed_seconds())) {
        return {false, "World preview elapsed time is not finite or bounded"};
    }
    world_state_.advance(elapsed);
    if (options_.camera_mode == WorldPreviewCameraMode::orbit && !update_camera()) {
        return {false, "World preview orbit produced an invalid camera"};
    }
    return {};
}

const client::ClientWorldState& WorldPreviewSceneSource::world_state() const noexcept
{
    return world_state_;
}

assets::AssetVector3 WorldPreviewSceneSource::world_center() const noexcept
{
    return world_center_;
}

float WorldPreviewSceneSource::world_radius() const noexcept
{
    return world_radius_;
}

const WorldPreviewSceneOptions& WorldPreviewSceneSource::options() const noexcept
{
    return options_;
}

bool WorldPreviewSceneSource::update_camera() noexcept
{
    auto direction = base_direction_;
    if (options_.camera_mode == WorldPreviewCameraMode::orbit) {
        const auto wrapped_angle = std::fmod(
            world_state_.elapsed_seconds() *
                static_cast<double>(options_.orbit_angular_velocity_radians_per_second),
            kFullOrbitRadians);
        const auto angle = static_cast<float>(wrapped_angle);
        const auto cosine = std::cos(angle);
        const auto sine = std::sin(angle);
        direction.x = base_direction_.x * cosine - base_direction_.y * sine;
        direction.y = base_direction_.x * sine + base_direction_.y * cosine;
    }

    const auto camera_distance = world_radius_ * options_.camera_distance_factor;
    const auto near_plane = std::max(kMinimumNearPlane, world_radius_ * 0.01F);
    const auto far_plane = std::max(
        near_plane + 1.0F,
        camera_distance + world_radius_ * 2.0F);
    world_state_.set_camera(client::RenderCameraState{
        add(world_center_, scale(direction, camera_distance)),
        world_center_,
        {0.0F, 0.0F, 1.0F},
        options_.vertical_field_of_view_radians,
        near_plane,
        far_plane,
    });
    const auto& camera = world_state_.camera();
    return renderer::is_valid(renderer::RenderCamera{
        camera.position,
        camera.target,
        camera.up,
        camera.vertical_field_of_view_radians,
        camera.near_plane,
        camera.far_plane,
    });
}

} // namespace hlclient::world_preview
