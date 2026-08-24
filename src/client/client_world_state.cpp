#include <hlclient/client/client_world_state.hpp>

#include <hlclient/world_render/world_render_types.hpp>

namespace hlclient::client {

void ClientWorldState::reset() noexcept
{
    elapsed_seconds_ = 0.0;
    connection_requested_ = false;
    clear_static_world();
    camera_ = {};
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
    static_world_ = std::move(package);
    world_revision_ = static_world_ ? static_world_->resource_revision() : 0U;
}

void ClientWorldState::clear_static_world() noexcept
{
    static_world_.reset();
    world_revision_ = 0U;
}

void ClientWorldState::set_camera(const RenderCameraState& camera) noexcept
{
    camera_ = camera;
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

const RenderCameraState& ClientWorldState::camera() const noexcept
{
    return camera_;
}

std::uint64_t ClientWorldState::world_revision() const noexcept
{
    return world_revision_;
}

const PreviewRenderOptions& ClientWorldState::preview_render_options() const noexcept
{
    return preview_render_options_;
}

} // namespace hlclient::client
