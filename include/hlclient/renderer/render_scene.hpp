#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <memory>
#include <optional>

namespace hlclient::world_render {
class WorldRenderPackage;
}

namespace hlclient::renderer {

struct ClearColor {
    float red{0.035F};
    float green{0.055F};
    float blue{0.085F};
    float alpha{1.0F};

    [[nodiscard]] friend bool operator==(const ClearColor&, const ClearColor&) = default;
};

struct RenderCamera {
    assets::AssetVector3 position{0.0F, -1.0F, 0.0F};
    assets::AssetVector3 target{0.0F, 0.0F, 0.0F};
    assets::AssetVector3 up{0.0F, 0.0F, 1.0F};
    float vertical_field_of_view_radians{1.0471975512F};
    float near_plane{0.1F};
    float far_plane{4'096.0F};
};

enum class RenderCullMode {
    none,
    back,
};

enum class RenderBaselineLightStylePolicy {
    source_slot_zero,
};

struct RenderStaticWorld {
    std::shared_ptr<const world_render::WorldRenderPackage> package;
    RenderCullMode cull_mode{RenderCullMode::none};
    RenderBaselineLightStylePolicy light_style_policy{
        RenderBaselineLightStylePolicy::source_slot_zero};
};

struct RenderScene {
    ClearColor clear_color{};
    RenderCamera camera{};
    std::optional<RenderStaticWorld> static_world;
};

struct RenderExtent {
    int width{0};
    int height{0};

    [[nodiscard]] friend bool operator==(const RenderExtent&, const RenderExtent&) = default;
};

} // namespace hlclient::renderer
