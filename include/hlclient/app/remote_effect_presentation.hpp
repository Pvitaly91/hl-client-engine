#pragma once
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/renderer/transient_visuals.hpp>

namespace hlclient::app {
// Executes committed game-selected requests through the same bounded world
// decal/shell mechanisms as local effects. No game names or weapon rules.
class RemoteEffectPresentation final {
public:
    void reset() noexcept;
    void consume(const game_api::RemoteWeaponEffectsBatch&, game_api::GameClientHost&,
        renderer::TransientVisuals&, WorldImpactPresentation&,
        std::shared_ptr<const collision::CollisionWorldPackage>,
        const world_render::WorldRenderPackage*,
        std::shared_ptr<const goldsrc::collision::BrushCollisionScene>,
        bool shell_ready, bool decal_ready, double now) noexcept;
    void present(renderer::RenderScene&, double now) noexcept;
    struct Statistics {
        std::uint64_t consumed{}, shells{}, shell_unavailable{}, impact_hits{},
            impact_rejected{}, flash_submissions{}, light_submissions{};
    };
    [[nodiscard]] const Statistics& statistics() const noexcept { return stats_; }
private:
    std::array<std::optional<game_api::RemoteWeaponEffect>,32> active_{};
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,128> seen_{};
    std::size_t next_seen_{};
    std::size_t next_{};
    Statistics stats_;
};
}
