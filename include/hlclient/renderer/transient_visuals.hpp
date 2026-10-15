#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <array>
#include <memory>
#include <span>

namespace hlclient::renderer {
enum class ParticleBurstStatus : std::uint8_t { spawned, duplicate, invalid, stale_generation, expired };
[[nodiscard]] constexpr std::string_view to_string(ParticleBurstStatus status) noexcept {
    switch(status) {
    case ParticleBurstStatus::spawned:return "spawned";
    case ParticleBurstStatus::duplicate:return "duplicate";
    case ParticleBurstStatus::invalid:return "invalid";
    case ParticleBurstStatus::stale_generation:return "stale_generation";
    case ParticleBurstStatus::expired:return "expired";
    }
    return "invalid";
}
struct ImpactParticleStatistics {
    std::uint64_t spawned{}, duplicate{}, invalid{}, stale{}, expired{}, evicted{}, output_limited{};
};
// Neutral, fixed-memory presentation only. Absolute-age trajectories make
// repeated updates and frame slicing deterministic; no visibility dependency.
class ImpactParticlePool final {
public:
    void reset() noexcept;
    [[nodiscard]] ParticleBurstStatus spawn(const game_api::LocalWeaponActionIdentity&,
        assets::AssetVector3 point, assets::AssetVector3 normal,
        const game_api::ImpactParticleProfile&, double now) noexcept;
    void update(double now) noexcept;
    void append_glows(RenderScene&) const noexcept;
    [[nodiscard]] std::span<const RenderWorldParticle> particles() const noexcept { return visible_; }
    [[nodiscard]] const ImpactParticleStatistics& statistics() const noexcept { return stats_; }
private:
    struct Burst {
        game_api::LocalWeaponActionIdentity action;
        game_api::ImpactParticleProfile profile;
        assets::AssetVector3 origin;
        std::array<assets::AssetVector3,16U> velocities{};
        double start{};
    };
    std::array<std::optional<Burst>,32U> bursts_{};
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,128U> seen_{};
    std::array<RenderWorldParticle,256U> buffer_{};
    std::span<const RenderWorldParticle> visible_{};
    std::size_t next_burst_{},next_seen_{};
    std::uint64_t generation_{};
    double now_{};
    ImpactParticleStatistics stats_;
};
struct TransientShellState final {
    assets::AssetVector3 position{}, velocity{};
    float yaw_degrees{};
    double starts_at_seconds{}, expires_at_seconds{};
    bool at_rest{};
    std::array<char,32> model_name{}; // owning approved virtual token, no gameplay identity
};
struct TransientVisualStatistics final {
    std::uint64_t shells_created{}, shells_expired{}, shells_dropped{},
        trace_failures{}, collision_contacts{}, duplicate_spawns{},
        contact_events{}, contact_events_dropped{}, contact_repeat_suppressed{};
    std::size_t active{}, capacity{32U};
};
// Render-only point-shell simulation. No gameplay collision writes. Integrates
// on a fixed 120 Hz step, with a 250 ms catch-up cap and bounded point sweeps.
class TransientVisuals final {
public:
    void reset() noexcept;
    void set_collision_world(std::shared_ptr<const collision::CollisionWorldPackage>) noexcept;
    [[nodiscard]] bool spawn(const game_api::LocalShellEjection&, double now) noexcept;
    void update(double now) noexcept;
    [[nodiscard]] std::span<const TransientShellState> shells() const noexcept { return visible_; }
    // Owning until the next update/reset; caller delivers once, independent of culling.
    [[nodiscard]] std::span<const game_api::LocalShellContact> contacts() const noexcept { return contacts_; }
    [[nodiscard]] TransientVisualStatistics statistics() const noexcept;
private:
    struct Entry {
        TransientShellState shell;
        game_api::LocalWeaponActionIdentity action;
        double simulated_at{}, last_contact_at{-1.0};
        std::uint32_t contact_ordinal{};
    };
    std::array<std::optional<Entry>,32U> entries_{};
    std::array<TransientShellState,32U> buffer_{};
    std::span<const TransientShellState> visible_{};
    std::array<game_api::LocalShellContact,32U> contact_buffer_{};
    std::span<const game_api::LocalShellContact> contacts_{};
    std::shared_ptr<const collision::CollisionWorldPackage> world_;
    collision::CollisionQueryScratch scratch_;
    TransientVisualStatistics stats_;
};
} // namespace hlclient::renderer
