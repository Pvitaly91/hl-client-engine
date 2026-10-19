#pragma once
#include <hlclient/game_api/game_client_module.hpp>
#include <hlclient/game_api/hud_resources.hpp>
#include <array>
#include <span>

namespace hlclient::games::halflife {
// Source-pixel to framebuffer-pixel scale. Tier selection stays independent of
// this local-compatible compact720 layout and of Windows display scaling.
[[nodiscard]] float halflife_hud_automatic_scale(game_api::HudPresentationContext,
    const game_api::HudResourceDescriptor*, std::uint32_t tier) noexcept;
[[nodiscard]] float halflife_hud_user_scale(game_api::HudPresentationContext) noexcept;
// Half-Life owns roles, suit/weapon visibility, menu policy and history. The
// application owns approved immutable resources; this class only retains the
// bounded neutral metadata and emits screen-space primitives.
class HalfLifeSpriteHud final {
public:
    void reset() noexcept;
    void bind(const game_api::HudResourceSet&);
    void committed(const game_api::GameRecordState&) noexcept;
    void selection_result(std::uint8_t,bool,double) noexcept;
    [[nodiscard]] game_api::HudMenuResult block_selection(const game_api::HudMenuInput&) noexcept;
    [[nodiscard]] game_api::HudMenuResult input(
        const client::RuntimeClientObservationState&, const game_api::HudMenuInput&,
        std::span<const std::uint8_t> eligible, game_api::HudPresentationContext);
    void compose(const client::RuntimeClientObservationState&, game_api::HudState&,
        game_api::HudPresentationContext, double effective_fov, double now);
    void append_audio(game_api::LocalAudioBatch&) noexcept;
    [[nodiscard]] const game_api::HudResourceDescriptor* base_descriptor() const noexcept {
        return descriptor("sprites/hud.txt");
    }
    [[nodiscard]] std::uint32_t base_tier(game_api::HudPresentationContext context) const noexcept {
        return tier(base_descriptor(),context);
    }
    [[nodiscard]] static std::span<const game_api::LocalSoundReference> sounds() noexcept;
    [[nodiscard]] static bool has_ammo(const client::RuntimeClientObservationState&,
        const client::RuntimeWeaponTypeObservation&) noexcept;
private:
    struct History final {
        game_api::GameRecordState::Pickup::Kind kind{};
        std::uint8_t id{}, amount{};
        std::array<char,64U> token{};
        std::optional<double> expires;
        bool occupied{};
    };
    void observe_boundary(const client::RuntimeClientObservationState&) noexcept;
    void observe_active_weapon(const client::RuntimeWeaponHudObservation&,
        const client::LocalPlayerLifecycle&) noexcept;
    [[nodiscard]] bool consume_confirmation_hold(const game_api::HudMenuInput&) noexcept;
    void sound(std::size_t,double) noexcept;
    [[nodiscard]] const game_api::HudResourceDescriptor* descriptor(std::string_view) const noexcept;
    [[nodiscard]] static std::uint32_t preferred_tier(game_api::HudPresentationContext) noexcept;
    [[nodiscard]] std::uint32_t tier(const game_api::HudResourceDescriptor*, game_api::HudPresentationContext) const noexcept;
    [[nodiscard]] const game_api::HudResourceEntry* role(const game_api::HudResourceDescriptor*,
        std::string_view,std::uint32_t) const noexcept;
    game_api::HudResourceSet resources_;
    std::array<History,4U> history_{};
    std::size_t history_next_{};
    std::array<std::optional<double>,8U> damage_expiry_{};
    // Four source sprite frames: front, right, rear, left. Fresh committed
    // Damage values wait only until this frame's presentation pose is ready.
    std::array<float,4U> pain_intensity_{};
    std::array<std::array<double,3>,16U> pending_pain_{};
    std::size_t pending_pain_count_{};
    std::optional<client::RuntimeObservationSource> damage_high_water_, reset_source_;
    std::uint64_t generation_{}, life_{}, deaths_{};
    bool menu_open_{};
    bool primary_confirmation_held_{};
    std::optional<std::uint8_t> candidate_, pending_;
    bool pending_queued_{};
    std::string selection_status_;
    std::optional<double> pending_started_;
    std::optional<client::RuntimeObservationSource> pending_source_;
    std::optional<std::uint8_t> pending_pre_active_;
    // Confirmed server transitions only, independent of menu/HUD visibility.
    std::optional<std::uint8_t> confirmed_weapon_, previous_weapon_;
    std::optional<client::RuntimeObservationSource> confirmed_weapon_source_;
    std::optional<std::uint8_t> menu_group_;
    std::optional<double> last_time_;
    std::optional<int> last_health_, last_armor_, last_clip_, last_reserve_;
    std::optional<std::uint8_t> last_weapon_;
    double health_fade_started_{}, armor_fade_started_{}, ammo_fade_started_{};
    std::array<game_api::LocalSoundCue,16U> cues_{};
    std::size_t cue_count_{};
    std::uint64_t ui_ordinal_{};
};
} // namespace hlclient::games::halflife
