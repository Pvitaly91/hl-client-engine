#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

namespace hlclient::world_render {
class WorldRenderPackage;
}

namespace hlclient::client {

struct RenderCameraState {
    assets::AssetVector3 position{0.0F, -1.0F, 0.0F};
    assets::AssetVector3 target{0.0F, 0.0F, 0.0F};
    assets::AssetVector3 up{0.0F, 0.0F, 1.0F};
    float vertical_field_of_view_radians{1.0471975512F};
    float near_plane{0.1F};
    float far_plane{4'096.0F};
};

enum class PreviewWorldCullMode {
    none,
    back,
};

struct PreviewRenderOptions {
    PreviewWorldCullMode cull_mode{PreviewWorldCullMode::none};
};

class ClientWorldState final {
public:
    void reset() noexcept;
    void advance(std::chrono::duration<double> elapsed) noexcept;
    void set_connection_requested(bool requested) noexcept;
    void set_static_world(
        std::shared_ptr<const world_render::WorldRenderPackage> package) noexcept;
    void clear_static_world() noexcept;
    void set_camera(const RenderCameraState& camera) noexcept;
    [[nodiscard]] bool set_preview_render_options(
        const PreviewRenderOptions& options) noexcept;

    [[nodiscard]] double elapsed_seconds() const noexcept;
    [[nodiscard]] bool connection_requested() const noexcept;
    [[nodiscard]] const std::shared_ptr<const world_render::WorldRenderPackage>&
    static_world() const noexcept;
    [[nodiscard]] const RenderCameraState& camera() const noexcept;
    [[nodiscard]] std::uint64_t world_revision() const noexcept;
    [[nodiscard]] const PreviewRenderOptions& preview_render_options() const noexcept;

private:
    double elapsed_seconds_{0.0};
    bool connection_requested_{false};
    std::shared_ptr<const world_render::WorldRenderPackage> static_world_;
    RenderCameraState camera_{};
    std::uint64_t world_revision_{0U};
    PreviewRenderOptions preview_render_options_{};
};

} // namespace hlclient::client
