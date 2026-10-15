#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/game_api/temporary_effects.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/renderer/transient_visuals.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
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
struct WorldImpactParticleDiagnostic {
    std::uint64_t serial{};
    game_api::LocalWeaponActionIdentity action;
    assets::AssetVector3 point{};
    std::uint32_t surface{},requested_count{};
    renderer::ParticleBurstStatus status{};
};
enum class ServerDecalStatus : std::uint8_t {
    submitted, invalid_request, duplicate, stale, target_unavailable,
    surface_unmapped, surface_ambiguous, surface_unsupported,
    material_rejected, lighting_unavailable, geometry_limit
};
[[nodiscard]] constexpr std::string_view to_string(ServerDecalStatus status) noexcept {
    switch(status) {
    case ServerDecalStatus::submitted: return "submitted";
    case ServerDecalStatus::invalid_request: return "invalid_request";
    case ServerDecalStatus::duplicate: return "duplicate";
    case ServerDecalStatus::stale: return "stale";
    case ServerDecalStatus::target_unavailable: return "target_unavailable";
    case ServerDecalStatus::surface_unmapped: return "surface_unmapped";
    case ServerDecalStatus::surface_ambiguous: return "surface_ambiguous";
    case ServerDecalStatus::surface_unsupported: return "surface_unsupported";
    case ServerDecalStatus::material_rejected: return "material_rejected";
    case ServerDecalStatus::lighting_unavailable: return "lighting_unavailable";
    case ServerDecalStatus::geometry_limit: return "geometry_limit";
    }
    return "invalid_request";
}
struct ServerDecalResult {
    ServerDecalStatus status{ServerDecalStatus::invalid_request};
    std::optional<std::uint32_t> source_surface_index;
    std::size_t vertex_count{};
    bool evicted{};
};
// Generic, map-scoped presentation storage. The game chooses eligibility and
// profile; this owner traces and clips onto one uniquely matched surface.
// Brush placements share the existing target-lifetime and rigid attachment path.
class WorldImpactPresentation final {
public:
    explicit WorldImpactPresentation(bool debug_freeze_tracer = false) noexcept
        : debug_freeze_tracer_(debug_freeze_tracer) {}
    struct LocalMuzzleAnchor {
        std::uint64_t generation{};
        std::uint32_t model_index{};
        assets::AssetVector3 point{};
    };
    // A committed position/target is materialized geometrically. It is not a
    // shot ray and never reads the current camera or infers gameplay hits.
    struct ServerDecalRequest {
        game_api::TemporaryEffectIdentity identity;
        std::uint64_t record_ordinal{}, resource_generation{};
        assets::AssetVector3 position{};
        std::uint32_t target_entity{};
        std::shared_ptr<const assets::WorldTextureAsset> texture;
        game_api::LocalDecalMaterialMode material_mode{
            game_api::LocalDecalMaterialMode::straight_alpha};
    };
    struct ServerDecalTarget {
        std::uint32_t entity_number{}, model_slot{}, source_model_index{};
        std::uint64_t lifetime{};
        renderer::RenderMatrix4 model_transform{};
    };
    struct ServerDecalTargetLifetime {
        std::uint32_t entity_number{};
        std::uint64_t lifetime{};
        std::uint32_t model_slot{}, source_model_index{};
    };
    // Borrowed only during submit. Collision selects the actual hit and pose;
    // the approved render library supplies its exact model surfaces. Neither
    // unavailable geometry nor a missing lifetime may redirect a hit behind it.
    struct BrushImpactContext {
        const world_scene_render::BrushSubmodelRenderLibrary* library{};
        std::span<const ServerDecalTargetLifetime> lifetimes;
        std::shared_ptr<const assets::WorldTextureAsset> texture;
        game_api::LocalDecalMaterialMode material_mode{};
    };
    void reset() noexcept;
    [[nodiscard]] WorldImpactResult submit(
        const game_api::LocalWorldImpactRequest&,
        std::shared_ptr<const collision::CollisionWorldPackage>,
        const world_render::WorldRenderPackage*,
        std::shared_ptr<const goldsrc::collision::BrushCollisionScene>,
        bool decal_resource_ready, double now,
        const BrushImpactContext* brush_context = nullptr) noexcept;
    [[nodiscard]] ServerDecalResult submit_server(
        const ServerDecalRequest&, const world_render::WorldRenderPackage*,
        std::span<const std::uint32_t> allowed_surface_indices = {},
        std::optional<ServerDecalTarget> = {}) noexcept;
    // Explicit current resource binding. Opaque resource revisions are never
    // ordered numerically; stale requests cannot switch the owner backwards.
    void bind_server_resources(std::uint64_t generation,
        std::uint64_t resource_generation) noexcept;
    // Caller supplies a COMPLETE committed live set using the existing neutral
    // entity-lifetime owner. Render-frame absence alone never retires a target.
    void retire_server_targets(std::span<const ServerDecalTargetLifetime>) noexcept;
    [[nodiscard]] std::vector<renderer::RenderWorldDecals> frame_batches(
        const world_scene_render::RuntimeBrushRenderFrame* = nullptr) const;
    [[nodiscard]] std::size_t server_active() const noexcept;
    void update(double now) noexcept;
    [[nodiscard]] std::optional<renderer::RenderWorldDecals> frame(
        std::shared_ptr<const assets::WorldTextureAsset>,
        game_api::LocalDecalMaterialMode mode=game_api::LocalDecalMaterialMode::straight_alpha);
    [[nodiscard]] std::size_t active() const noexcept;
    [[nodiscard]] std::size_t pending() const noexcept;
    void append_lines(renderer::RenderScene&,std::optional<LocalMuzzleAnchor> = {}) noexcept;
    void append_impact_glows(renderer::RenderScene& scene) const noexcept { particles_.append_glows(scene); }
    [[nodiscard]] std::span<const renderer::RenderWorldParticle> particles() const noexcept { return particles_.particles(); }
    [[nodiscard]] const renderer::ImpactParticleStatistics& particle_statistics() const noexcept { return particles_.statistics(); }
    [[nodiscard]] const auto& particle_diagnostics() const noexcept { return particle_diagnostics_; }
private:
    struct Entry {
        game_api::LocalWeaponActionIdentity action;
        std::vector<renderer::RenderDecalVertex> vertices;
        double expires_at{};
        double publish_at{};
        bool published{};
        struct ServerPlacement {
            game_api::TemporaryEffectIdentity identity;
            std::shared_ptr<const assets::WorldTextureAsset> texture;
            game_api::LocalDecalMaterialMode material_mode{};
            std::optional<ServerDecalTarget> target;
            std::shared_ptr<const std::vector<renderer::RenderDecalVertex>> geometry;
            // Revision of THIS immutable clipped mesh. Pool mutations do not
            // invalidate GPU geometry owned by an unrelated placement.
            std::uint64_t geometry_revision{};
        };
        std::optional<ServerPlacement> server;
        // Local ray impacts use the same immutable geometry/attachment shape,
        // but keep their action identity, delayed publication and 20s lifetime.
        std::optional<ServerPlacement> brush;
    };
    std::array<std::optional<Entry>,64U> entries_{};
    // Independent of target retirement and decal-pool reuse. Replayed source
    // actions cannot repaint a replacement brush occupant within this window.
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,64U> seen_brush_actions_{};
    std::size_t next_seen_brush_{};
    std::size_t next_entry_{};
    std::uint64_t revision_{1};
    bool dirty_{true};
    std::shared_ptr<const std::vector<renderer::RenderDecalVertex>> cached_;
    std::uint64_t server_generation_{}, server_resource_generation_{};
    struct ServerReceipt {
        game_api::TemporaryEffectIdentity identity;
        std::array<std::uint64_t,3> order{};
    };
    std::array<std::optional<ServerReceipt>,128U> seen_server_{};
    std::size_t next_seen_server_{};
    std::array<std::uint64_t,3> server_stale_floor_{};
    collision::CollisionQueryScratch scratch_;
    renderer::ImpactParticlePool particles_;
    std::array<std::optional<WorldImpactParticleDiagnostic>,32U> particle_diagnostics_{};
    std::uint64_t particle_serial_{};
    struct LineEntry {
        game_api::LocalWeaponActionIdentity action;
        renderer::RenderWorldLine line;
        double expires_at{};
        bool follow_local_muzzle{};
        float speed{},segment_length{};
        std::optional<assets::AssetVector3> launch_origin;
        double flight_started_at{};
        game_api::TransientGlowProfile head_glow;
        float start_offset_units{};
        std::uint64_t serial{};
    };
    std::array<std::optional<LineEntry>,64U> lines_{};
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,128U> seen_lines_{};
    std::size_t next_line_{},next_seen_line_{};
    double line_now_{};
    bool debug_freeze_tracer_{};
    // Exactly one owning world-space inspection sample; no scene/asset pointers.
    // Replaced by the latest newly materialized solid moving tracer, reset with map/session.
    std::optional<renderer::RenderWorldLine> debug_frozen_line_;
    std::uint64_t line_serial_{},debug_frozen_serial_{};
};
} // namespace hlclient::app
