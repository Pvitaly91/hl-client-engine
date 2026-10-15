#pragma once
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/game_api/audio.hpp>
namespace hlclient::games::halflife {
// Behavior reference: Valve SDK b1b5cf5892918535619b2937bb927e46cb097ba1,
// CHgun and EV_HornetGunFire. These are presentation policies, never AI/physics.
enum class HornetFireMode : std::uint8_t { track, fast };
inline std::optional<HornetFireMode> hornet_fire_mode(int value) noexcept {
    if(value==0) return HornetFireMode::track;
    if(value==1) return HornetFireMode::fast;
    return {};
}
inline constexpr std::uint8_t hornet_maximum_ammo=8;
inline constexpr std::uint32_t hornet_shoot_sequence=5;
inline bool hornet_command(std::string_view name) noexcept { return name=="weapon_hornetgun"; }
inline double hornet_recharge_interval(std::uint32_t maximum_clients) noexcept {
    return maximum_clients>1 ? .3 : .5;
}
inline bool hornet_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    // Original approved v_hgun label spelling is case-sensitive, unlike the
    // SDK semantic enum. Do not apply these numbers to arbitrary Studio data.
    constexpr std::array<std::string_view,6> labels{
        "idle1","fidgetSway","fidgetShake","down","up","Shoot"};
    if(model.resource_name!="models/v_hgun.mdl" || model.sequences.size()!=labels.size()) return false;
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& seq=model.sequences[i];
        if(seq.label!=labels[i] || !seq.frame_count || !std::isfinite(seq.fps) || seq.fps<=0) return false;
    }
    return model.supported_bodies[0] && model.supported_bodies[1];
}
inline std::string_view hornet_launch_sound(const game_api::LocalWeaponActionIdentity& action) noexcept {
    constexpr std::array<std::string_view,3> samples{
        "agrunt/ag_fire1.wav","agrunt/ag_fire2.wav","agrunt/ag_fire3.wav"};
    return samples[shotgun_fire_variant(action)%samples.size()];
}
inline std::optional<game_api::LocalSoundReference> hornet_sound(std::string_view name) noexcept {
    if(name!="agrunt/ag_fire1.wav" && name!="agrunt/ag_fire2.wav" && name!="agrunt/ag_fire3.wav") return {};
    game_api::LocalSoundReference result;
    result.source=game_api::SoundReferenceSource::module_profile_dependency;
    std::copy(name.begin(),name.end(),result.sample.begin());
    return result;
}
inline double hornet_visual_pitch(const game_api::LocalWeaponActionIdentity& action) noexcept {
    // Same integer range/sign/axis as V_PunchAxis(0, RandomLong(0,2)), with an
    // action-stable independent distribution; not mutable stock RNG parity.
    return static_cast<double>((shotgun_fire_variant(action)>>8)%3U);
}
}
