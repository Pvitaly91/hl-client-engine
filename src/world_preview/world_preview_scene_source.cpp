#include <hlclient/world_preview/world_preview_scene_source.hpp>

#include <hlclient/core/log.hpp>
#include <hlclient/renderer/render_camera_math.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace hlclient::world_preview {
namespace {

constexpr float kMinimumNearPlane = 0.1F;
constexpr float kMaximumOrbitAngularVelocity = 1.0F;
constexpr double kFullOrbitRadians = 6.28318530717958647692;

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return renderer::is_finite(bounds.minimum) &&
        renderer::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool valid_camera_mode(const WorldPreviewCameraMode mode) noexcept
{
    return mode == WorldPreviewCameraMode::static_camera ||
        mode == WorldPreviewCameraMode::orbit ||
        mode == WorldPreviewCameraMode::spawn ||
        mode == WorldPreviewCameraMode::free_flight ||
        mode == WorldPreviewCameraMode::entity_first_person;
}

[[nodiscard]] bool valid_visibility_mode(
    const world_visibility::WorldVisibilityMode mode) noexcept
{
    return mode == world_visibility::WorldVisibilityMode::all ||
        mode == world_visibility::WorldVisibilityMode::frustum_only ||
        mode == world_visibility::WorldVisibilityMode::pvs_only ||
        mode == world_visibility::WorldVisibilityMode::pvs_and_frustum;
}

[[nodiscard]] bool valid_fallback_policy(
    const world_visibility::WorldPvsFallbackPolicy policy) noexcept
{
    return policy == world_visibility::WorldPvsFallbackPolicy::fail_closed ||
        policy == world_visibility::WorldPvsFallbackPolicy::frustum_only ||
        policy == world_visibility::WorldPvsFallbackPolicy::all_surfaces;
}

[[nodiscard]] bool valid_brush_mode(
    const WorldPreviewBrushSubmodelsMode mode) noexcept
{
    return mode == WorldPreviewBrushSubmodelsMode::off ||
        mode == WorldPreviewBrushSubmodelsMode::static_instances;
}

[[nodiscard]] std::uint32_t fallback_reason_bit(
    const world_visibility::WorldPvsFallbackReason reason) noexcept
{
    switch (reason) {
    case world_visibility::WorldPvsFallbackReason::none:
        return 0U;
    case world_visibility::WorldPvsFallbackReason::camera_in_leaf_zero:
        return 1U << 0U;
    case world_visibility::WorldPvsFallbackReason::camera_in_solid_leaf:
        return 1U << 1U;
    case world_visibility::WorldPvsFallbackReason::camera_point_query_failed:
        return 1U << 2U;
    case world_visibility::WorldPvsFallbackReason::pvs_row_unavailable:
        return 1U << 3U;
    case world_visibility::WorldPvsFallbackReason::visibility_data_absent:
        return 1U << 4U;
    }
    return 0U;
}

void log_fallback_warning(
    const world_visibility::WorldPvsFallbackReason reason) noexcept
{
    using world_visibility::WorldPvsFallbackReason;
    switch (reason) {
    case WorldPvsFallbackReason::camera_in_leaf_zero:
        core::log(core::LogLevel::warning,
            "[visibility] PVS fallback=camera_in_leaf_zero");
        break;
    case WorldPvsFallbackReason::camera_in_solid_leaf:
        core::log(core::LogLevel::warning,
            "[visibility] PVS fallback=camera_in_solid_leaf");
        break;
    case WorldPvsFallbackReason::camera_point_query_failed:
        core::log(core::LogLevel::warning,
            "[visibility] PVS fallback=camera_point_query_failed");
        break;
    case WorldPvsFallbackReason::pvs_row_unavailable:
        core::log(core::LogLevel::warning,
            "[visibility] PVS fallback=pvs_row_unavailable");
        break;
    case WorldPvsFallbackReason::visibility_data_absent:
        core::log(core::LogLevel::warning,
            "[visibility] PVS fallback=visibility_data_absent");
        break;
    case WorldPvsFallbackReason::none:
        break;
    }
}

[[nodiscard]] assets::AssetVector3 add(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

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

[[nodiscard]] renderer::RenderCamera render_camera(
    const client::RenderCameraState& camera) noexcept
{
    return {
        camera.position,
        camera.target,
        camera.up,
        camera.vertical_field_of_view_radians,
        camera.near_plane,
        camera.far_plane,
    };
}

[[nodiscard]] bool same_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool same_camera(
    const client::RenderCameraState& left,
    const client::RenderCameraState& right) noexcept
{
    return same_vector(left.position, right.position) &&
        same_vector(left.target, right.target) &&
        same_vector(left.up, right.up) &&
        left.vertical_field_of_view_radians ==
            right.vertical_field_of_view_radians &&
        left.near_plane == right.near_plane &&
        left.far_plane == right.far_plane;
}

} // namespace

WorldPreviewSceneSource::WorldPreviewSceneSource(
    std::shared_ptr<const world_render::WorldRenderPackage> package,
    WorldPreviewSceneOptions options)
    : options_{std::move(options)}
{
    if (!package) {
        throw std::invalid_argument{
            "World preview requires an immutable render package"};
    }
    validate_options(false);
    initialize_bounds(package->bounds());
    world_state_.set_static_world(std::move(package));
    if (!world_state_.set_preview_render_options(
            client::PreviewRenderOptions{options_.cull_mode})) {
        throw std::invalid_argument{"World preview culling mode is invalid"};
    }
    if (!update_camera()) {
        throw std::invalid_argument{"World preview derived camera is invalid"};
    }
}

WorldPreviewSceneSource::WorldPreviewSceneSource(
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> package,
    WorldPreviewSceneOptions options)
    : options_{std::move(options)}
{
    if (!package) {
        throw std::invalid_argument{
            "World preview requires an immutable scene render package"};
    }
    validate_options(true);
    initialize_bounds(package->bounds());
    world_state_.set_world_scene(std::move(package));
    if (!world_state_.set_preview_render_options(
            client::PreviewRenderOptions{options_.cull_mode})) {
        throw std::invalid_argument{"World preview culling mode is invalid"};
    }
    build_scene_adapters();
    if (!update_camera()) {
        throw std::invalid_argument{"World preview derived camera is invalid"};
    }
    const auto visibility = update_visibility();
    if (!visibility) {
        throw std::runtime_error{visibility.error};
    }
}

client::SceneUpdateResult WorldPreviewSceneSource::update(
    const client::FrameTime elapsed)
{
    if (!std::isfinite(elapsed.count()) ||
        (elapsed.count() > 0.0 &&
            elapsed.count() >
                std::numeric_limits<double>::max() -
                    world_state_.elapsed_seconds())) {
        return {false, "World preview elapsed time is not finite or bounded"};
    }
    world_state_.advance(elapsed);
    if (options_.camera_mode == WorldPreviewCameraMode::orbit &&
        !update_camera()) {
        return {false, "World preview orbit produced an invalid camera"};
    }
    const bool externally_controlled_camera =
        options_.camera_mode == WorldPreviewCameraMode::free_flight ||
        options_.camera_mode == WorldPreviewCameraMode::entity_first_person;
    const auto& current_visibility = world_state_.world_visibility();
    const auto& current_draw_list = world_state_.visible_draw_list();
    if (externally_controlled_camera && last_visibility_camera_ &&
        last_visibility_extent_ && current_visibility && current_draw_list &&
        current_visibility == last_visibility_state_ &&
        current_draw_list == last_visible_draw_list_ &&
        same_camera(*last_visibility_camera_, world_state_.camera()) &&
        last_visibility_extent_->width == options_.visibility_extent.width &&
        last_visibility_extent_->height == options_.visibility_extent.height) {
        return {};
    }
    return update_visibility();
}

const client::ClientWorldState& WorldPreviewSceneSource::world_state() const noexcept
{
    return world_state_;
}

void WorldPreviewSceneSource::publish_camera_seed(
    const client::RenderCameraState& camera) noexcept
{
    world_state_.publish_camera_seed(camera);
}

bool WorldPreviewSceneSource::publish_interactive_camera(
    const client::RenderCameraState& camera,
    const client::InteractiveCameraMetadata& metadata) noexcept
{
    return world_state_.publish_interactive_camera(camera, metadata);
}

bool WorldPreviewSceneSource::publish_dynamic_entities(
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package,
    std::shared_ptr<const entity_render::EntityRenderFrame> frame) noexcept
{
    return world_state_.set_dynamic_entities(
        std::move(package), std::move(frame));
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

bool WorldPreviewSceneSource::spawn_camera_applied() const noexcept
{
    return spawn_camera_applied_;
}

std::uint32_t WorldPreviewSceneSource::fallback_warning_count() const noexcept
{
    return fallback_warning_count_;
}

client::SceneUpdateResult WorldPreviewSceneSource::set_render_extent(
    const renderer::RenderExtent extent)
{
    // A minimized window has no drawable frame; retain the last valid extent
    // until the runtime publishes a positive size again.
    if (extent.width < 0 || extent.height < 0) {
        return {false, "World preview render extent is negative"};
    }
    if (extent.width == 0 || extent.height == 0) {
        return {};
    }
    options_.visibility_extent = extent;
    return {};
}

void WorldPreviewSceneSource::validate_options(
    const bool scene_package_available) const
{
    const auto direction = renderer::normalize(options_.isometric_direction);
    const bool cull_mode_valid =
        options_.cull_mode == client::PreviewWorldCullMode::none ||
        options_.cull_mode == client::PreviewWorldCullMode::back;
    if (!valid_camera_mode(options_.camera_mode) || !cull_mode_valid ||
        !valid_visibility_mode(options_.visibility_mode) ||
        !valid_fallback_policy(options_.pvs_fallback_policy) ||
        !valid_brush_mode(options_.brush_submodels) || !direction ||
        !std::isfinite(options_.minimum_radius) ||
        options_.minimum_radius <= 0.0F ||
        !std::isfinite(options_.camera_distance_factor) ||
        options_.camera_distance_factor <= 1.0F ||
        !std::isfinite(options_.orbit_angular_velocity_radians_per_second) ||
        options_.orbit_angular_velocity_radians_per_second < 0.0F ||
        options_.orbit_angular_velocity_radians_per_second >
            kMaximumOrbitAngularVelocity ||
        !std::isfinite(options_.vertical_field_of_view_radians) ||
        options_.visibility_extent.width <= 0 ||
        options_.visibility_extent.height <= 0) {
        throw std::invalid_argument{
            "World preview camera or visibility configuration is invalid"};
    }
    if (!scene_package_available &&
        (options_.visibility_mode !=
                world_visibility::WorldVisibilityMode::all ||
            options_.brush_submodels != WorldPreviewBrushSubmodelsMode::off)) {
        throw std::invalid_argument{
            "PVS/frustum and brush visibility require a scene render package"};
    }
    if (options_.spawn_camera) {
        const auto forward = renderer::normalize(options_.spawn_camera->forward);
        const auto up = renderer::normalize(options_.spawn_camera->up);
        if (!renderer::is_finite(options_.spawn_camera->position) || !forward ||
            !up || !renderer::normalize(renderer::cross(*forward, *up))) {
            throw std::invalid_argument{
                "World preview spawn camera descriptor is invalid"};
        }
    }
}

void WorldPreviewSceneSource::initialize_bounds(
    const assets::WorldBounds& bounds)
{
    if (!valid_bounds(bounds)) {
        throw std::invalid_argument{
            "World preview bounds must be finite and ordered"};
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
    const auto diagonal_radius =
        std::sqrt(renderer::dot(half_extent, half_extent));
    const auto direction = renderer::normalize(options_.isometric_direction);
    if (!renderer::is_finite(world_center_) ||
        !std::isfinite(diagonal_radius) || !direction) {
        throw std::invalid_argument{
            "World preview derived bounds are not finite"};
    }
    world_radius_ = std::max(diagonal_radius, options_.minimum_radius);
    base_direction_ = *direction;
}

void WorldPreviewSceneSource::build_scene_adapters()
{
    const auto& scene = world_state_.world_scene();
    if (!scene) {
        return;
    }
    visibility_brush_instances_.reserve(scene->brush_instances().size());
    visible_brush_instances_.reserve(scene->brush_instances().size());
    for (const auto& instance : scene->brush_instances()) {
        const bool supported = instance.support_status == world_scene_render::
                BrushSubmodelRenderSupportStatus::supported_static_opaque &&
            instance.source_model_index.has_value();
        visibility_brush_instances_.push_back({
            instance.source_instance_index,
            instance.transformed_bounds,
            instance.touched_leaf_indices,
            supported,
        });
        if (supported) {
            visible_brush_instances_.push_back({
                instance.source_instance_index,
                *instance.source_model_index,
                instance.model_transform,
            });
        }
    }
    const auto& library = scene->brush_library();
    if (!library.render_package()) {
        return;
    }
    visible_brush_models_.reserve(library.models().size());
    for (const auto& model : library.models()) {
        visible_brush_models_.push_back({
            model.source_model_index(),
            library.render_package()->indices().size(),
            library.render_package()->materials().size(),
            model.surfaces(),
        });
    }
}

bool WorldPreviewSceneSource::update_camera() noexcept
{
    if (options_.camera_mode == WorldPreviewCameraMode::spawn &&
        options_.spawn_camera) {
        const auto direction = renderer::normalize(options_.spawn_camera->forward);
        const auto up = renderer::normalize(options_.spawn_camera->up);
        if (!direction || !up) {
            return false;
        }
        const auto center_delta =
            subtract(world_center_, options_.spawn_camera->position);
        const auto center_distance =
            std::sqrt(renderer::dot(center_delta, center_delta));
        const auto near_plane =
            std::max(kMinimumNearPlane, world_radius_ * 0.01F);
        const auto far_plane = std::max(
            near_plane + 1.0F,
            center_distance + world_radius_ * 2.0F);
        world_state_.set_camera(client::RenderCameraState{
            options_.spawn_camera->position,
            add(options_.spawn_camera->position, *direction),
            *up,
            options_.vertical_field_of_view_radians,
            near_plane,
            far_plane,
        });
        spawn_camera_applied_ = true;
        return renderer::is_valid(render_camera(world_state_.camera()));
    }

    spawn_camera_applied_ = false;
    auto direction = base_direction_;
    if (options_.camera_mode == WorldPreviewCameraMode::orbit) {
        const auto wrapped_angle = std::fmod(
            world_state_.elapsed_seconds() *
                static_cast<double>(
                    options_.orbit_angular_velocity_radians_per_second),
            kFullOrbitRadians);
        const auto angle = static_cast<float>(wrapped_angle);
        const auto cosine = std::cos(angle);
        const auto sine = std::sin(angle);
        direction.x = base_direction_.x * cosine - base_direction_.y * sine;
        direction.y = base_direction_.x * sine + base_direction_.y * cosine;
    }

    const auto camera_distance = world_radius_ * options_.camera_distance_factor;
    const auto near_plane =
        std::max(kMinimumNearPlane, world_radius_ * 0.01F);
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
    return renderer::is_valid(render_camera(world_state_.camera()));
}

client::SceneUpdateResult WorldPreviewSceneSource::update_visibility()
{
    const auto& scene = world_state_.world_scene();
    if (!scene) {
        return {};
    }
    if (next_visibility_revision_ == 0U) {
        return {false, "World preview visibility revision is exhausted"};
    }

    try {
        const world_visibility::WorldVisibilityResolveInput resolve_input{
            &scene->spatial_package(),
            scene->world_surfaces(),
            visibility_brush_instances_,
            render_camera(world_state_.camera()),
            options_.visibility_extent,
            options_.visibility_mode,
            options_.pvs_fallback_policy,
            next_visibility_revision_,
            scene->visibility_scene_identity(),
            options_.brush_submodels ==
                WorldPreviewBrushSubmodelsMode::static_instances,
        };
        const world_visibility::WorldVisibilityResolver resolver;
        auto resolved = resolver.resolve(resolve_input);
        if (!resolved || !resolved.visibility) {
            const auto message = resolved.error
                ? resolved.error->message
                : std::string{"unknown visibility failure"};
            return {false, "World preview visibility failed: " + message};
        }
        auto visibility =
            std::make_shared<const world_visibility::WorldVisibilitySet>(
                std::move(*resolved.visibility));
        const auto fallback_reason = visibility->fallback_reason();

        const auto& world_package = scene->world_package();
        if (!world_package) {
            return {false, "World preview scene has no world render package"};
        }
        const world_visibility::WorldVisibleDrawListBuildInput draw_input{
            visibility.get(),
            scene->world_surfaces(),
            world_package->indices().size(),
            world_package->materials().size(),
            visible_brush_models_,
            visible_brush_instances_,
        };
        const world_visibility::WorldVisibleDrawListBuilder draw_builder;
        auto built = draw_builder.build(draw_input);
        if (!built || !built.draw_list) {
            const auto message = built.error
                ? built.error->message
                : std::string{"unknown draw-list failure"};
            return {false, "World preview draw-list failed: " + message};
        }
        auto draw_list =
            std::make_shared<const world_visibility::WorldVisibleDrawList>(
                std::move(*built.draw_list));
        if (!world_state_.set_world_visibility(
                std::move(visibility), std::move(draw_list))) {
            return {false, "World preview visibility publication was rejected"};
        }
        last_visibility_camera_ = world_state_.camera();
        last_visibility_extent_ = options_.visibility_extent;
        last_visibility_state_ = world_state_.world_visibility();
        last_visible_draw_list_ = world_state_.visible_draw_list();
        const auto warning_bit = fallback_reason_bit(fallback_reason);
        if (warning_bit != 0U &&
            (reported_fallback_reason_mask_ & warning_bit) == 0U) {
            log_fallback_warning(fallback_reason);
            reported_fallback_reason_mask_ |= warning_bit;
            ++fallback_warning_count_;
        }
        next_visibility_revision_ =
            next_visibility_revision_ ==
                    std::numeric_limits<std::uint64_t>::max()
            ? 0U
            : next_visibility_revision_ + 1U;
        return {};
    } catch (const std::bad_alloc&) {
        return {false, "World preview visibility allocation is unavailable"};
    } catch (const std::length_error&) {
        return {false, "World preview visibility exceeds container limits"};
    }
}

} // namespace hlclient::world_preview
