#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <array>

namespace hlclient::games::halflife {
// Session-owned companion to B1/E2. Core owns simulation and drawing.
class WeaponVisuals final {
public:
    void reset() noexcept;
    [[nodiscard]] bool bind(const std::optional<game_api::LocalWeaponModelMetadata>&);
    void remember_submission(const game_api::LocalWeaponSubmittedCommand&) noexcept;
    void observe(const game_api::LocalWeaponPresentationSnapshot&, double) noexcept;
    void cancel() noexcept;
    [[nodiscard]] game_api::LocalVisualFrame frame(const game_api::LocalVisualContext&) noexcept;
private:
    std::optional<game_api::LocalWeaponModelMetadata> model_;
    std::optional<game_api::LocalMuzzleFlash> flash_;
    std::optional<game_api::LocalMuzzleLight> light_;
    struct SubmittedShot {
        std::uint64_t generation{};
        std::uint32_t sequence{};
        game_api::LocalWeaponSubmittedCommand::ShotContext context;
    };
    std::array<std::optional<SubmittedShot>,32U> submitted_{};
    std::size_t next_submitted_{};
    std::optional<game_api::LocalWorldImpactRequest> pending_impact_;
    // Bounded recent-action identity guard; never a growing shot journal.
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,32U> seen_{};
    std::optional<game_api::LocalWeaponActionIdentity> last_seen_;
    std::size_t next_seen_{};
    bool shell_pending_{};
    game_api::LocalVisualStatistics statistics_;
};
}
