#pragma once
#include <hlclient/game_api/game_client_module.hpp>

namespace hlclient::games::halflife {
// Server-triggered visual state. No prediction/physics or inferred map timers.
class HalfLifeScreenEffects final {
public:
    void reset() noexcept { *this={}; }
    void committed(const game_api::GameRecordState&) noexcept;
    [[nodiscard]] game_api::ScreenEffectsFrame frame(double) const noexcept;
private:
    struct Shake { game_api::GameRecordState::ScreenShake value; double start{}; };
    struct Fade { game_api::GameRecordState::ScreenFade value; double start{}; };
    std::optional<Shake> shake_;
    std::optional<Fade> fade_;
    std::optional<client::RuntimeObservationSource> shake_high_water_,fade_high_water_;
};
}
