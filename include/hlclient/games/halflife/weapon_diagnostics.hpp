#pragma once
#include <hlclient/game_api/weapon_diagnostics.hpp>
#include <algorithm>

namespace hlclient::games::halflife {
// Compiled profiles used by production hook classification, not server slot
// guesses. Ammo/event/model indices remain negotiated. Hook fixtures enumerate
// this same registry but independently assert the expected fourteen IDs/modes.
inline constexpr auto weapon_diagnostic_capabilities=[] {
    using K=core::MultiplayerActionKind;
    using C=game_api::WeaponDiagnosticCapability;
    return std::array<C,14>{{
        {1,"weapon_crowbar","models/v_crowbar.mdl",{K::melee,K::unknown,K::unknown,K::deploy,K::holster},true,false},
        {2,"weapon_9mmhandgun","models/v_9mmhandgun.mdl",{K::fire,K::fire,K::reload,K::deploy,K::holster},true,false},
        {3,"weapon_357","models/v_357.mdl",{K::fire,K::zoom,K::reload,K::deploy,K::holster},true,false},
        {4,"weapon_9mmAR","models/v_9mmAR.mdl",{K::fire,K::projectile,K::reload,K::deploy,K::holster},true,true},
        {6,"weapon_crossbow","models/v_crossbow.mdl",{K::projectile,K::zoom,K::reload,K::deploy,K::holster},true,true},
        {7,"weapon_shotgun","models/v_shotgun.mdl",{K::pellets,K::pellets,K::reload,K::deploy,K::holster},true,false},
        {8,"weapon_rpg","models/v_rpg.mdl",{K::projectile,K::guidance,K::reload,K::deploy,K::holster},true,true},
        {9,"weapon_gauss","models/v_gauss.mdl",{K::beam,K::charge,K::unknown,K::deploy,K::holster},true,false},
        {10,"weapon_egon","models/v_egon.mdl",{K::beam,K::unknown,K::unknown,K::deploy,K::holster},true,true},
        {11,"weapon_hornetgun","models/v_hgun.mdl",{K::projectile,K::projectile,K::unknown,K::deploy,K::holster},true,true},
        {12,"weapon_handgrenade","models/v_grenade.mdl",{K::throw_object,K::unknown,K::unknown,K::deploy,K::holster},false,true},
        {13,"weapon_tripmine","models/v_tripmine.mdl",{K::place,K::unknown,K::unknown,K::deploy,K::holster},false,true},
        {14,"weapon_satchel","models/v_satchel.mdl",{K::throw_object,K::detonate,K::unknown,K::deploy,K::holster},false,true},
        {15,"weapon_snark","models/v_squeak.mdl",{K::throw_object,K::unknown,K::unknown,K::deploy,K::holster},false,true}
    }};
}();
[[nodiscard]] inline const game_api::WeaponDiagnosticCapability* weapon_diagnostic_capability(std::uint32_t id) noexcept {
    const auto found=std::ranges::find(weapon_diagnostic_capabilities,id,&game_api::WeaponDiagnosticCapability::weapon_id);
    return found==weapon_diagnostic_capabilities.end() ? nullptr : &*found;
}
[[nodiscard]] inline game_api::WeaponDiagnosticObjectProfile weapon_diagnostic_object_profile(std::string_view model) noexcept {
    using P=game_api::WeaponDiagnosticObjectProfile;
    // Exact public SDK resource roles only. Several roles also have pickup
    // models: this classifies appearance, never proves a launched projectile.
    if(model=="models/grenade.mdl" || model=="models/w_grenade.mdl")return P::grenade;
    if(model=="models/crossbow_bolt.mdl")return P::bolt;
    if(model=="models/rpgrocket.mdl")return P::rocket;
    if(model=="models/hornet.mdl")return P::hornet;
    if(model=="models/w_satchel.mdl")return P::satchel;
    if(model=="models/v_tripmine.mdl")return P::tripmine;
    if(model=="models/w_squeak.mdl")return P::snark;
    return P::unknown;
}
}
