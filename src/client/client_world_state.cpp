#include <hlclient/client/client_world_state.hpp>

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visible_draw_list.hpp>
#include <hlclient/world_visibility/world_visibility_types.hpp>

#include <utility>

namespace hlclient::client {
namespace {

[[nodiscard]] bool valid_camera(const RenderCameraState& camera) noexcept
{
    return renderer::is_valid(renderer::RenderCamera{
        camera.position,
        camera.target,
        camera.up,
        camera.vertical_field_of_view_radians,
        camera.near_plane,
        camera.far_plane,
    });
}

[[nodiscard]] bool valid_interactive_metadata(
    const InteractiveCameraMetadata& metadata) noexcept
{
    if (metadata.input_revision == 0U || metadata.camera_revision == 0U) {
        return false;
    }
    switch (metadata.mode) {
    case InteractiveCameraMode::free_flight_world:
        return !metadata.controlled_entity &&
            metadata.controlled_entity_status ==
                ControlledEntityCameraStatus::not_applicable;
    case InteractiveCameraMode::entity_first_person:
        return metadata.controlled_entity.has_value() &&
            *metadata.controlled_entity != 0U &&
            (metadata.controlled_entity_status ==
                    ControlledEntityCameraStatus::anchored ||
                metadata.controlled_entity_status ==
                    ControlledEntityCameraStatus::anchor_missing);
    }
    return false;
}

} // namespace

void ClientWorldState::reset() noexcept
{
    elapsed_seconds_ = 0.0;
    connection_requested_ = false;
    clear_static_world();
    clear_dynamic_entities();
    camera_ = {};
    interactive_camera_metadata_.reset();
    preview_render_options_ = {};
}

void ClientWorldState::advance(const std::chrono::duration<double> elapsed) noexcept
{
    if (elapsed.count() > 0.0) {
        elapsed_seconds_ += elapsed.count();
    }
}

void ClientWorldState::set_connection_requested(const bool requested) noexcept
{
    connection_requested_ = requested;
}

void ClientWorldState::set_static_world(
    std::shared_ptr<const world_render::WorldRenderPackage> package) noexcept
{
    if (entity_scene_ && entity_scene_->world_scene_association()) {
        clear_dynamic_entities();
    }
    world_scene_.reset();
    scene_revision_ = 0U;
    clear_world_visibility();
    static_world_ = std::move(package);
    world_revision_ = static_world_ ? static_world_->resource_revision() : 0U;
}

void ClientWorldState::set_world_scene(
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        package) noexcept
{
    clear_world_visibility();
    world_scene_ = std::move(package);
    if (world_scene_) {
        static_world_ = world_scene_->world_package();
        world_revision_ = static_world_ ? static_world_->resource_revision() : 0U;
        scene_revision_ = world_scene_->resource_revision();
    } else {
        static_world_.reset();
        world_revision_ = 0U;
        scene_revision_ = 0U;
    }
    if (entity_scene_) {
        const auto association = entity_scene_->world_scene_association();
        if (association &&
            (!world_scene_ || association->resource_id !=
                    world_scene_->resource_id() ||
                association->revision != world_scene_->resource_revision())) {
            clear_dynamic_entities();
        }
    }
}

void ClientWorldState::clear_static_world() noexcept
{
    if (entity_scene_ && entity_scene_->world_scene_association()) {
        clear_dynamic_entities();
    }
    static_world_.reset();
    world_scene_.reset();
    world_revision_ = 0U;
    scene_revision_ = 0U;
    clear_world_visibility();
}

bool ClientWorldState::set_world_visibility(
    std::shared_ptr<const world_visibility::WorldVisibilitySet> visibility,
    std::shared_ptr<const world_visibility::WorldVisibleDrawList> draw_list) noexcept
{
    if (!visibility && !draw_list) {
        clear_world_visibility();
        return true;
    }
    if (!world_scene_ || !visibility || !draw_list ||
        visibility->revision() == 0U ||
        visibility->revision() != draw_list->visibility_revision() ||
        visibility->scene_identity() != draw_list->scene_identity() ||
        !visibility->result_signature() ||
        visibility->result_signature() != draw_list->result_signature()) {
        return false;
    }
    const auto expected_identity =
        world_scene_->visibility_scene_identity();
    if (visibility->scene_identity() != expected_identity) {
        return false;
    }
    world_visibility_ = std::move(visibility);
    visible_draw_list_ = std::move(draw_list);
    visibility_revision_ = world_visibility_->revision();
    return true;
}

void ClientWorldState::clear_world_visibility() noexcept
{
    world_visibility_.reset();
    visible_draw_list_.reset();
    visibility_revision_ = 0U;
}

bool ClientWorldState::set_dynamic_entities(
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package,
    std::shared_ptr<const entity_render::EntityRenderFrame> frame) noexcept
{
    if (!package && !frame) {
        clear_dynamic_entities();
        return true;
    }
    if (!package || !frame || package->resource_id() == 0U ||
        package->resource_revision() == 0U || frame->resource_id() == 0U ||
        frame->resource_revision() == 0U ||
        frame->scene_package_identity() !=
            entity_render::EntityRenderResourceIdentity{
                package->resource_id(), package->resource_revision()}) {
        return false;
    }
    const auto world_association = package->world_scene_association();
    if (world_association &&
        (!world_scene_ ||
            world_association->resource_id != world_scene_->resource_id() ||
            world_association->revision !=
                world_scene_->resource_revision())) {
        return false;
    }
    entity_scene_ = std::move(package);
    entity_frame_ = std::move(frame);
    entity_scene_revision_ = entity_scene_->resource_revision();
    entity_frame_revision_ = entity_frame_->resource_revision();
    return true;
}

void ClientWorldState::clear_dynamic_entities() noexcept
{
    entity_scene_.reset();
    entity_frame_.reset();
    entity_scene_revision_ = 0U;
    entity_frame_revision_ = 0U;
}

void ClientWorldState::set_camera(const RenderCameraState& camera) noexcept
{
    camera_ = camera;
    interactive_camera_metadata_.reset();
}

bool ClientWorldState::set_interactive_camera(
    const RenderCameraState& camera,
    const InteractiveCameraMetadata& metadata) noexcept
{
    if (!valid_camera(camera) || !valid_interactive_metadata(metadata)) {
        return false;
    }
    if (interactive_camera_metadata_) {
        const auto& previous = *interactive_camera_metadata_;
        if (metadata.input_revision <= previous.input_revision ||
            metadata.camera_revision < previous.camera_revision ||
            (metadata.camera_revision == previous.camera_revision &&
                camera != camera_)) {
            return false;
        }
    }
    camera_ = camera;
    interactive_camera_metadata_ = metadata;
    return true;
}

void ClientWorldState::clear_interactive_camera_metadata() noexcept
{
    interactive_camera_metadata_.reset();
}

bool ClientWorldState::set_preview_render_options(
    const PreviewRenderOptions& options) noexcept
{
    if (options.cull_mode != PreviewWorldCullMode::none &&
        options.cull_mode != PreviewWorldCullMode::back) {
        return false;
    }
    preview_render_options_ = options;
    return true;
}

double ClientWorldState::elapsed_seconds() const noexcept
{
    return elapsed_seconds_;
}

bool ClientWorldState::connection_requested() const noexcept
{
    return connection_requested_;
}

const std::shared_ptr<const world_render::WorldRenderPackage>&
ClientWorldState::static_world() const noexcept
{
    return static_world_;
}

const std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>&
ClientWorldState::world_scene() const noexcept
{
    return world_scene_;
}

const std::shared_ptr<const world_visibility::WorldVisibilitySet>&
ClientWorldState::world_visibility() const noexcept
{
    return world_visibility_;
}

const std::shared_ptr<const world_visibility::WorldVisibleDrawList>&
ClientWorldState::visible_draw_list() const noexcept
{
    return visible_draw_list_;
}

const RenderCameraState& ClientWorldState::camera() const noexcept
{
    return camera_;
}

const std::optional<InteractiveCameraMetadata>&
ClientWorldState::interactive_camera_metadata() const noexcept
{
    return interactive_camera_metadata_;
}

std::uint64_t ClientWorldState::input_revision() const noexcept
{
    return interactive_camera_metadata_
        ? interactive_camera_metadata_->input_revision
        : 0U;
}

std::uint64_t ClientWorldState::camera_revision() const noexcept
{
    return interactive_camera_metadata_
        ? interactive_camera_metadata_->camera_revision
        : 0U;
}

const std::shared_ptr<const entity_render::EntitySceneRenderPackage>&
ClientWorldState::entity_scene() const noexcept
{
    return entity_scene_;
}

const std::shared_ptr<const entity_render::EntityRenderFrame>&
ClientWorldState::entity_frame() const noexcept
{
    return entity_frame_;
}

std::uint64_t ClientWorldState::world_revision() const noexcept
{
    return world_revision_;
}

std::uint64_t ClientWorldState::scene_revision() const noexcept
{
    return scene_revision_;
}

std::uint64_t ClientWorldState::visibility_revision() const noexcept
{
    return visibility_revision_;
}

std::uint64_t ClientWorldState::entity_scene_revision() const noexcept
{
    return entity_scene_revision_;
}

std::uint64_t ClientWorldState::entity_frame_revision() const noexcept
{
    return entity_frame_revision_;
}

const PreviewRenderOptions& ClientWorldState::preview_render_options() const noexcept
{
    return preview_render_options_;
}

} // namespace hlclient::client
