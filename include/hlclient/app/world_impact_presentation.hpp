#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace hlclient::app {
enum class WorldImpactStatus : std::uint8_t {
    hit, invalid_request, expired, collision_unavailable, trace_failed,
    start_solid, miss, unsupported_blocker, surface_unmapped,
    surface_ambiguous, surface_unsupported, geometry_limit
};
struct WorldImpactResult {
    WorldImpactStatus status{WorldImpactStatus::invalid_request};
    std::optional<assets::AssetVector3> point;
    std::optional<game_api::LocalWorldSurfaceHit> surface;
};
// Generic, map-scoped presentation storage. The game chooses eligibility and
// profile; this owner only traces and clips onto a uniquely matched static
// render surface. The nearest committed brush blocker wins over the world.
class WorldImpactPresentation final {
public:
    void reset() noexcept;
    [[nodiscard]] WorldImpactResult submit(
        const game_api::LocalWorldImpactRequest&,
        std::shared_ptr<const collision::CollisionWorldPackage>,
        const world_render::WorldRenderPackage*,
        std::shared_ptr<const goldsrc::collision::BrushCollisionScene>,
        bool decal_resource_ready, double now) noexcept;
    void update(double now) noexcept;
    [[nodiscard]] std::optional<renderer::RenderWorldDecals> frame(
        std::shared_ptr<const assets::WorldTextureAsset>,
        game_api::LocalDecalMaterialMode mode=game_api::LocalDecalMaterialMode::straight_alpha);
    [[nodiscard]] std::size_t active() const noexcept;
    [[nodiscard]] std::size_t pending() const noexcept;
private:
    struct Entry {
        game_api::LocalWeaponActionIdentity action;
        std::vector<renderer::RenderDecalVertex> vertices;
        double expires_at{};
        double publish_at{};
        bool published{};
    };
    std::array<std::optional<Entry>,64U> entries_{};
    std::size_t next_entry_{};
    std::uint64_t revision_{1};
    bool dirty_{true};
    std::shared_ptr<const std::vector<renderer::RenderDecalVertex>> cached_;
    collision::CollisionQueryScratch scratch_;
};
} // namespace hlclient::app
