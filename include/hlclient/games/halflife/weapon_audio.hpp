#pragma once
#include <hlclient/game_api/audio.hpp>
#include <hlclient/game_api/presentation.hpp>
namespace hlclient::games::halflife {
// Companion to the existing B1 controller, not a second action/eligibility owner.
class WeaponAudio final {
public:
    void bind(const std::optional<game_api::LocalWeaponModelMetadata>&);
    void action(const game_api::LocalWeaponActionIdentity&) noexcept;
    void cancel() noexcept;
    void update(const game_api::LocalWeaponPresentationSnapshot&,
        const std::optional<game_api::LocalWeaponModelMetadata>&,double now,bool enabled) noexcept;
    game_api::LocalAudioBatch drain() noexcept;
private:
    void emit(game_api::LocalSoundReference,game_api::LocalSoundKind,double,float=1,float=1) noexcept;
    game_api::LocalAudioBatch batch_;
    std::uint64_t serial_{}, restart_{}, action_command_{}, cancelled_restart_{};
    double previous_{-1}, last_clock_{}, start_{};
    std::uint32_t sequence_{};
    bool armed_{true};
    struct ConsumedMarker { std::size_t ordinal{}; std::uint64_t loop{}; };
    std::array<ConsumedMarker,64> consumed_{};
    std::size_t consumed_count_{};
};
}
