#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/game_api/remote_effects.hpp>
#include <array>

namespace hlclient::games::halflife {
// Local-compatible warm spark burst; not proprietary engine parity.
[[nodiscard]] constexpr game_api::ImpactParticleProfile bullet_impact_particles() noexcept {
    return {16U,0.28F,42.0F,78.0F,180.0F,0.60F,{1.0F,0.72F,0.26F,0.9F},
        1.2F,true,{2.0F,64.0F,0.55F},0.06F};
}
// Local-compatible presentation variation, not a stock physics constant.
// Choose once per causal action; sampling/render frequency never changes it.
[[nodiscard]] constexpr float mp5_tracer_start_offset(
    const game_api::LocalWeaponActionIdentity& action) noexcept {
    const auto mix=[](std::uint64_t value) constexpr noexcept {
        value^=value>>30; value*=0xbf58476d1ce4e5b9ULL;
        value^=value>>27; value*=0x94d049bb133111ebULL;
        return value^(value>>31);
    };
    const auto variation=mix(mix(action.generation+0x9e3779b97f4a7c15ULL) ^
        mix(action.canonical_pre_revision+0x243f6a8885a308d3ULL) ^
        mix((static_cast<std::uint64_t>(action.emitter_entity)<<32) |
            action.command_sequence));
    constexpr float maximum_offset_units=16.0F;
    return static_cast<float>(variation&0xffffffULL) *
        (maximum_offset_units/static_cast<float>(0xffffffULL));
}
// Session-owned companion to B1/E2. Core owns simulation and drawing.
class WeaponVisuals final {
public:
    void reset() noexcept;
    [[nodiscard]] bool bind(const std::optional<game_api::LocalWeaponModelMetadata>&);
    void remember_submission(const game_api::LocalWeaponSubmittedCommand&) noexcept;
    void remember_committed_shot(const std::optional<game_api::LocalWeaponActionIdentity>&,
        const game_api::CommittedScriptedEvent&) noexcept;
    void observe(const game_api::LocalWeaponPresentationSnapshot&, double, bool sniper=false) noexcept;
    void cancel() noexcept;
    void reset_fire_cadence() noexcept { tracer_count_=0; }
    [[nodiscard]] game_api::LocalVisualFrame frame(const game_api::LocalVisualContext&) noexcept;
private:
    std::optional<game_api::LocalWeaponModelMetadata> model_;
    std::optional<game_api::LocalMuzzleFlash> flash_;
    std::optional<game_api::LocalMuzzleLight> light_;
    struct SubmittedShot {
        std::uint64_t generation{};
        std::uint32_t sequence{};
        game_api::LocalWeaponSubmittedCommand::ShotContext context;
        std::optional<double> committed_at;
    };
    std::array<std::optional<SubmittedShot>,32U> submitted_{};
    std::size_t next_submitted_{};
    std::optional<game_api::LocalWorldImpactRequest> pending_impact_;
    std::array<game_api::LocalWorldImpactRequest,7> pending_extra_impacts_{};
    std::size_t pending_extra_impact_count_{};
    std::optional<game_api::LocalWeaponSubmittedCommand::ShotContext> shell_context_;
    bool double_shell_{};
    // Bounded recent-action identity guard; never a growing shot journal.
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,32U> seen_{};
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,32U> seen_sniper_{};
    std::optional<game_api::LocalWeaponActionIdentity> last_seen_;
    std::size_t next_seen_{};
    std::size_t next_sniper_{};
    bool shell_pending_{};
    std::uint64_t tracer_count_{};
    game_api::LocalVisualStatistics statistics_;
};
}
