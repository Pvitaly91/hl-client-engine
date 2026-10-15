#pragma once
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/game_api/temporary_effects.hpp>

namespace hlclient::games::halflife {
inline game_api::LocalSoundReference crossbow_sound(std::string_view name) noexcept {
    game_api::LocalSoundReference r;
    r.source=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    if(name.size()<r.sample.size()) std::copy(name.begin(),name.end(),r.sample.begin());
    return r;
}
inline float crossbow_pitch(const game_api::LocalWeaponActionIdentity& a) noexcept {
    return static_cast<float>(93U+shotgun_fire_variant(a)%16U)/100.0F;
}
inline float crossbow_mechanical_volume(const game_api::LocalWeaponActionIdentity& a) noexcept {
    return .95F+static_cast<float>(shotgun_fire_variant(a)%51U)/1000.0F;
}
// Local-compatible gravity sparks, shared by committed TE_SPARKS and the
// sniper event's static-world hit. No stock engine RNG/ricochet sprite claim.
inline game_api::ImpactParticleProfile crossbow_sparks() noexcept {
    game_api::ImpactParticleProfile p;
    p.count=8;p.lifetime_seconds=.45F;p.minimum_speed_units=60;p.maximum_speed_units=140;
    p.gravity_units=400;p.radius_units=.65F;p.color={1,.65F,.2F,.9F};
    p.emissive=true;p.radial_distribution=true;p.flicker=true;return p;
}
} // namespace hlclient::games::halflife
