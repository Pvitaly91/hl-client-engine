#pragma once
#include <hlclient/game_api/action_evidence.hpp>

namespace hlclient::games::halflife {
// Interpretation of the existing scripted action diagnostic. Renderer frame
// probes remain generic and consume only action slot/source identities.
class HalfLifeActionEvidence final {
public:
    void observe(const client::RuntimeClientObservationState& observation,
                 const std::optional<game_api::GameActionTraffic>& traffic) noexcept;
    void reset() noexcept { *this = {}; }
    [[nodiscard]] const game_api::GameActionEvidenceSnapshot& snapshot() const noexcept {
        return snapshot_;
    }
private:
    game_api::GameActionEvidenceSnapshot snapshot_;
    std::optional<std::int32_t> last_health_, last_armor_;
    bool use_window_started_{};
    std::optional<client::RuntimeObservationSource> last_action_client_source_;
    std::uint64_t last_action_hud_revision_{};
    std::optional<std::int32_t> last_glock_clip_;
    std::optional<bool> last_glock_in_reload_;
    std::optional<std::uint8_t> last_glock_reserve_;
};
}
