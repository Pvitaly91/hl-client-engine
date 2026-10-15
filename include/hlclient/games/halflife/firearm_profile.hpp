#pragma once
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/local_visuals.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <string_view>
namespace hlclient::games::halflife {
enum class Firearm { unsupported, glock, mp5, shotgun, python, crossbow, rpg };
// Explicit compatibility table for the observed original frame-zero markers.
// The SDK forwards these numbers to an engine callback whose implementation
// is not public; this table is not a claim of binary R_MuzzleFlash parity.
inline std::optional<game_api::MuzzleSpriteProfile> muzzle_sprite_profile(
    std::uint32_t marker, std::uint32_t seed) noexcept {
    game_api::MuzzleSpriteProfile result;
    std::string_view name;
    switch(marker) {
    case 11: name="sprites/muzzleflash2.spr";result.scale=.1F;break;
    case 20: name="sprites/muzzleflash1.spr";result.scale=.2F;break;
    case 31: name="sprites/muzzleflash2.spr";result.scale=.3F;break;
    case 41: name="sprites/muzzleflash2.spr";result.scale=.4F;break;
    default:return {};
    }
    std::copy(name.begin(),name.end(),result.virtual_name.begin());
    result.frame_seed=seed;return result;
}
// Pinned CGlock::GlockFire sets BOTH attack timers to the selected cycle.
// EV_FireGlock2 uses GLOCK_SHOOT even for the last round (unlike Glock1).
inline constexpr double glock_primary_cycle_seconds=0.3;
inline constexpr double glock_secondary_cycle_seconds=0.2;
inline constexpr std::uint32_t glock_secondary_sequence=3U;
inline Firearm firearm_model(std::string_view name) noexcept {
    if(name=="models/v_9mmhandgun.mdl") return Firearm::glock;
    if(name=="models/v_9mmAR.mdl") return Firearm::mp5;
    if(name=="models/v_shotgun.mdl") return Firearm::shotgun;
    if(name=="models/v_357.mdl") return Firearm::python;
    if(name=="models/v_crossbow.mdl") return Firearm::crossbow;
    if(name=="models/v_rpg.mdl") return Firearm::rpg;
    return Firearm::unsupported;
}
inline bool mp5_command(std::string_view name) noexcept {
    return name=="weapon_9mmAR" || name=="weapon_mp5";
}
inline bool mp5_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    constexpr std::array<std::string_view,8> labels{"longidle","idle1","grenade","reload","deploy","shoot","shoot","shoot"};
    if(model.resource_name!="models/v_9mmAR.mdl" || model.sequences.size()!=labels.size()) return false;
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& s=model.sequences[i];
        if(s.label!=labels[i] || s.frame_count==0 || !std::isfinite(s.fps) || s.fps<=0) return false;
    }
    return model.supported_bodies[0] && model.supported_bodies[2];
}
enum class ShotgunRole : std::uint32_t {
    idle=0, single_fire=1, double_fire=2, reload_insert=3, reload_finish=4,
    reload_start=5, deploy=6, holster=7, idle4=8, idle_deep=9
};
inline constexpr double shotgun_single_cycle_seconds=0.75;
inline constexpr double shotgun_double_cycle_seconds=1.5;
inline bool shotgun_command(std::string_view name) noexcept { return name=="weapon_shotgun"; }
inline bool python_command(std::string_view name) noexcept {
    return name=="weapon_357" || name=="weapon_python";
}
inline bool crossbow_command(std::string_view name) noexcept { return name=="weapon_crossbow"; }
inline bool rpg_command(std::string_view name) noexcept { return name=="weapon_rpg"; }
inline bool rpg_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    // RPG_FIRE2 is the SDK role, but the approved v_rpg asset labels it "fire".
    constexpr std::array<std::string_view,10> labels{"idle","fidget","reload","fire",
        "holster1","draw1","holster2","draw2","idle2","fidget2"};
    if(model.resource_name!="models/v_rpg.mdl" || model.sequences.size()!=labels.size()) return false;
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& s=model.sequences[i];
        if(s.label!=labels[i] || !s.frame_count || !std::isfinite(s.fps) || s.fps<=0) return false;
    }
    return model.supported_bodies[0] && model.supported_bodies[1];
}
inline bool crossbow_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    constexpr std::array<std::string_view,12> labels{"idle1","idle2","fidget1","fidget2",
        "fire1","fire2","fire3","reload","draw1","draw2","holster1","holster2"};
    if(model.resource_name!="models/v_crossbow.mdl" || model.sequences.size()!=labels.size()) return false;
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& s=model.sequences[i];
        if(s.label!=labels[i] || !s.frame_count || !std::isfinite(s.fps) || s.fps<=0) return false;
    }
    return model.supported_bodies[0] && model.supported_bodies[1];
}
// Both pinned event callbacks select no new animation when BOTH post-shot
// values are zero. Retain that distinction instead of inventing FIRE3.
inline std::optional<std::uint32_t> crossbow_fire_sequence(int clip,int reserve) noexcept {
    if(clip>0) return 4U;
    if(reserve>0) return 6U;
    return {};
}
inline bool python_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    constexpr std::array<std::string_view,8> labels{
        "idle1","fidget1","fire1","reload","holster","draw","idle2","idle3"};
    if(model.resource_name!="models/v_357.mdl" || model.sequences.size()!=labels.size()) return false;
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& s=model.sequences[i];
        if(s.label!=labels[i] || !s.frame_count || !std::isfinite(s.fps) || s.fps<=0) return false;
    }
    return model.supported_bodies[0] && model.supported_bodies[1];
}
inline bool shotgun_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    constexpr std::array<std::string_view,10> labels{
        "sm_idle","shoot","shoot_big","reload","pump","start_reload","draw","reholster","idle4","deepidle"};
    if(model.resource_name!="models/v_shotgun.mdl" || model.sequences.size()!=labels.size()) return false;
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& seq=model.sequences[i];
        if(seq.label!=labels[i] || !seq.frame_count || !std::isfinite(seq.fps) || seq.fps<=0) return false;
    }
    return model.supported_bodies[0] && model.supported_bodies[2];
}
// Independently authored, action-stable triangular samples truncated to a disk.
// Mirrors the reference distribution shape, NOT the mutable stock RNG stream.
inline assets::AssetVector3 shotgun_pellet_direction(
    const game_api::LocalWeaponActionIdentity& action,std::uint32_t ordinal,
    assets::AssetVector3 forward,assets::AssetVector3 right,assets::AssetVector3 up,
    bool double_shot) noexcept {
    const auto mix=[](std::uint64_t v) noexcept {
        v^=v>>30;v*=0xbf58476d1ce4e5b9ULL;v^=v>>27;v*=0x94d049bb133111ebULL;return v^(v>>31);
    };
    // The causal event time is part of identity, not render/wall-clock time.
    // Committed-only actions have command_sequence=0 and can share the same
    // canonical revision, so that command/revision pair alone is insufficient.
    const auto occurrence=std::bit_cast<std::uint64_t>(
        action.started_at_seconds==0.0 ? 0.0 : action.started_at_seconds);
    auto seed=mix(action.generation)^mix(action.canonical_pre_revision)^mix(
        (std::uint64_t{action.emitter_entity}<<32)|action.command_sequence)^mix(ordinal+1U)^
        mix(occurrence)^mix(static_cast<std::uint64_t>(action.kind));
    const auto random=[&]() {
        seed=mix(seed+0x9e3779b97f4a7c15ULL);
        return static_cast<float>(seed&0xffffffU)/16777216.0F-0.5F;
    };
    float x=0,y=0;
    for(unsigned attempt=0;attempt<128;++attempt) {
        x=random()+random();y=random()+random();
        if(x*x+y*y<=1.0F) break;
        if(attempt==127) x=y=0; // bounded pathological fallback, never more traces
    }
    x*=double_shot ? 0.17365F : 0.08716F;y*=0.04362F;
    auto d=assets::AssetVector3{forward.x+right.x*x+up.x*y,
        forward.y+right.y*x+up.y*y,forward.z+right.z*x+up.z*y};
    const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    if(std::isfinite(length) && length>0) {d.x/=length;d.y/=length;d.z/=length;}
    return d;
}
inline std::uint32_t firearm_variant(const game_api::LocalWeaponActionIdentity& action) noexcept {
    auto x=action.command_sequence*2654435761U ^ static_cast<std::uint32_t>(action.generation) ^
        (action.emitter_entity*0x85ebca6bU);
    x^=x>>16;x*=0x7feb352dU;x^=x>>15;return x;
}
inline std::uint32_t shotgun_fire_variant(const game_api::LocalWeaponActionIdentity& action) noexcept {
    // Keep legacy Glock/MP5 variation unchanged. Shotgun committed-only events
    // need their own stable occurrence even without a local command number.
    auto x=std::bit_cast<std::uint64_t>(
        action.started_at_seconds==0.0 ? 0.0 : action.started_at_seconds)^
        (std::uint64_t{firearm_variant(action)}<<32)^action.canonical_pre_revision^
        static_cast<std::uint64_t>(action.kind);
    x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;x^=x>>31;
    return static_cast<std::uint32_t>(x^(x>>32));
}
inline float shotgun_fire_volume(const game_api::LocalWeaponActionIdentity& action) noexcept {
    const auto v=shotgun_fire_variant(action);
    return action.kind==game_api::LocalWeaponAction::secondary_fire ?
        0.98F+static_cast<float>(v%21U)/1000.0F : 0.95F+static_cast<float>(v%51U)/1000.0F;
}
inline float shotgun_fire_pitch(const game_api::LocalWeaponActionIdentity& action) noexcept {
    return static_cast<float>((action.kind==game_api::LocalWeaponAction::secondary_fire ? 85U : 93U)+
        shotgun_fire_variant(action)%32U)/100.0F;
}
inline std::string_view python_sound(const game_api::LocalWeaponActionIdentity& action) noexcept {
    return (shotgun_fire_variant(action)&1U) ? "weapons/357_shot2.wav" : "weapons/357_shot1.wav";
}
inline float python_volume(const game_api::LocalWeaponActionIdentity& action) noexcept {
    return 0.8F+static_cast<float>(shotgun_fire_variant(action)%101U)/1000.0F;
}
inline std::string_view mp5_sound(const game_api::LocalWeaponActionIdentity& action) noexcept {
    return (firearm_variant(action)&1U) ? "weapons/hks2.wav" : "weapons/hks1.wav";
}
// Pinned EV_FireMP52 profile. Stable per action/event, not SDK RNG parity.
inline std::string_view mp5_launch_sound(const game_api::LocalWeaponActionIdentity& action) noexcept {
    return (firearm_variant(action)&1U) ? "weapons/glauncher2.wav" : "weapons/glauncher.wav";
}
} // namespace hlclient::games::halflife
