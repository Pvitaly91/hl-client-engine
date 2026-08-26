#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace hlclient::world_render {
class WorldRenderPackage;
}

namespace hlclient::world_scene_render {
class WorldSceneRenderPackage;
}

namespace hlclient::world_visibility {
class WorldVisibleDrawList;
}

namespace hlclient::entity_render {
class EntitySceneRenderPackage;
class EntityRenderFrame;
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

struct RenderStaticWorldVisibilitySummary {
    std::uint64_t revision{0U};
    std::uint64_t scene_resource_id{0U};
    std::uint64_t scene_resource_revision{0U};
    std::uint64_t visibility_input_signature{0U};
    std::uint64_t draw_input_signature{0U};
    std::uint64_t result_signature_first{0U};
    std::uint64_t result_signature_second{0U};
    std::size_t visible_world_surface_count{0U};
    std::size_t visible_brush_instance_count{0U};
};

struct RenderStaticWorld {
    std::shared_ptr<const world_render::WorldRenderPackage> package;
    RenderCullMode cull_mode{RenderCullMode::none};
    RenderBaselineLightStylePolicy light_style_policy{
        RenderBaselineLightStylePolicy::source_slot_zero};
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        scene_package;
    std::shared_ptr<const world_visibility::WorldVisibleDrawList>
        visible_draw_list;
    std::optional<RenderStaticWorldVisibilitySummary> visibility_summary;
};

struct RenderDynamicEntityVisibilitySummary {
    std::size_t candidate_count{0U};
    std::size_t visible_count{0U};
    std::size_t studio_instance_count{0U};
    std::size_t sprite_instance_count{0U};
    std::size_t unsupported_instance_count{0U};
};

struct RenderDynamicEntities {
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package;
    std::shared_ptr<const entity_render::EntityRenderFrame> frame;
    RenderDynamicEntityVisibilitySummary visibility_summary{};
};

struct RenderScene {
    ClearColor clear_color{};
    RenderCamera camera{};
    std::optional<RenderStaticWorld> static_world;
    std::optional<RenderDynamicEntities> dynamic_entities;
};

struct RenderExtent {
    int width{0};
    int height{0};

    [[nodiscard]] friend bool operator==(const RenderExtent&, const RenderExtent&) = default;
};

} // namespace hlclient::renderer
