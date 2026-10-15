#pragma once
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
namespace hlclient::games::halflife {
struct RpgLauncherState {
    std::optional<bool> spot_active;
    std::optional<unsigned> active_rockets;
    [[nodiscard]] bool can_reload_or_holster() const noexcept {
        return spot_active && active_rockets && (!*spot_active || !*active_rockets);
    }
};
// Pinned CRpg's UpdateClientData publishes y/z, not the number of visible
// rocket entities. Unknown/invalid state fails closed for provisional policy.
inline RpgLauncherState rpg_launcher_state(const client::RuntimeClientObservationState& s) noexcept {
    RpgLauncherState result;
    if(!s.receiving_client || s.client_metadata.generation!=s.generation ||
        s.client_metadata.freshness==client::RuntimeObservationFreshness::unavailable) return result;
    const auto& v=s.receiving_client->auxiliary_vector2;
    if(v.y && (*v.y==0 || *v.y==1)) result.spot_active=*v.y==1;
    if(v.z && std::isfinite(*v.z) && *v.z>=0 && *v.z<=511 && std::floor(*v.z)==*v.z)
        result.active_rockets=static_cast<unsigned>(*v.z);
    return result;
}
inline bool rpg_can_select(const client::RuntimeClientObservationState& s,std::uint8_t target) noexcept {
    if(s.weapon_hud.active_weapon_id!=8U || target==8U) return true;
    const auto type=std::ranges::find(s.weapon_hud.catalogue,8U,&client::RuntimeWeaponTypeObservation::id);
    return type!=s.weapon_hud.catalogue.end() && rpg_command(type->command_name) &&
        rpg_launcher_state(s).can_reload_or_holster();
}
}
