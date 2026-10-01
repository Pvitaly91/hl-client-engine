#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/local_visuals.hpp>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hlclient::world_render {
class WorldRenderPackage;
}

namespace hlclient::world_scene_render {
class WorldSceneRenderPackage;
struct RuntimeBrushRenderFrame;
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

    [[nodiscard]] friend constexpr bool operator==(
        const RenderCamera& left,
        const RenderCamera& right) noexcept
    {
        return left.position.x == right.position.x &&
            left.position.y == right.position.y &&
            left.position.z == right.position.z &&
            left.target.x == right.target.x &&
            left.target.y == right.target.y &&
            left.target.z == right.target.z &&
            left.up.x == right.up.x && left.up.y == right.up.y &&
            left.up.z == right.up.z &&
            left.vertical_field_of_view_radians ==
                right.vertical_field_of_view_radians &&
            left.near_plane == right.near_plane &&
            left.far_plane == right.far_plane;
    }
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
    std::shared_ptr<const world_scene_render::RuntimeBrushRenderFrame> runtime_brushes;
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

// Viewmodel-local, pose-attached short-lived neutral effect. No game rule or
// action is interpreted by the renderer. Drawn after first-person geometry,
// before HUD, with first-person depth still enabled.
struct RenderMuzzleFlash {
    assets::AssetVector3 center{};
    float radius_units{};
    std::array<float, 4> color{};
};

// One bounded, renderer-neutral transient point light. World and first-person
// passes use separate centers because their projection spaces are distinct.
struct RenderPointLight {
    assets::AssetVector3 center{};
    float radius_units{};
    float intensity{};
    std::array<float, 3> color{};
};

struct RenderDecalVertex {
    assets::AssetVector3 position{};
    assets::AssetVector2 uv{};
};
struct RenderWorldDecals {
    // Immutable map-scoped texture and revisioned clipped world triangles.
    std::shared_ptr<const assets::WorldTextureAsset> texture;
    std::shared_ptr<const std::vector<RenderDecalVertex>> vertices;
    std::uint64_t revision{};
    game_api::LocalDecalMaterialMode material_mode{game_api::LocalDecalMaterialMode::straight_alpha};
};

[[nodiscard]] inline bool valid_render_point_light(const RenderPointLight& light) noexcept {
    if (!std::isfinite(light.center.x) || !std::isfinite(light.center.y) ||
        !std::isfinite(light.center.z) || !std::isfinite(light.radius_units) ||
        !std::isfinite(light.intensity) || light.radius_units<=0.0F ||
        light.radius_units>4096.0F || light.intensity<0.0F ||
        light.intensity>4.0F) return false;
    for (const auto channel:light.color)
        if (!std::isfinite(channel) || channel<0.0F || channel>4.0F) return false;
    return true;
}

using RenderBasicHud = game_api::HudDrawCommands;

struct RenderScene {
    ClearColor clear_color{};
    RenderCamera camera{};
    std::optional<RenderStaticWorld> static_world;
    std::optional<RenderDynamicEntities> dynamic_entities;
    // Camera-bound Studio frame. Drawn after the world with its own depth
    // buffer contents; it never enters world entity visibility/PVS.
    std::optional<RenderDynamicEntities> first_person_entities;
    std::optional<RenderDynamicEntities> transient_world_entities;
    std::optional<RenderMuzzleFlash> first_person_flash;
    // World-space billboards share the flash mechanism, before first-person
    // depth reset. Fixed capacity is independent of the number of players.
    std::array<RenderMuzzleFlash,32> world_flashes{};
    std::size_t world_flash_count{};
    std::optional<RenderPointLight> transient_world_light;
    std::optional<RenderPointLight> transient_first_person_light;
    std::optional<RenderWorldDecals> world_decals;
    // Second bounded neutral material batch; game semantics stay upstream.
    std::optional<RenderWorldDecals> secondary_world_decals;
    std::optional<RenderBasicHud> basic_hud;
};

struct RenderExtent {
    int width{0};
    int height{0};

    [[nodiscard]] friend bool operator==(const RenderExtent&, const RenderExtent&) = default;
};

} // namespace hlclient::renderer
