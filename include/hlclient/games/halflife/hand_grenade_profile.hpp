#pragma once
#include <hlclient/game_api/presentation.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
namespace hlclient::games::halflife {
// Behavioral facts from pinned Valve SDK b1b5cf5892918535619b2937bb927e46cb097ba1.
// These calculations select a local pose/diagnostic only. They never create,
// move, collide or schedule the detonation of a world grenade.
inline constexpr std::uint8_t hand_grenade_maximum_ammo=10;
inline constexpr std::uint8_t hand_grenade_default_give=5;
inline bool hand_grenade_command(std::string_view name) noexcept { return name=="weapon_handgrenade"; }
inline bool hand_grenade_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    constexpr std::array<std::string_view,8> labels{
        "idle","fidget","pinpull","throw1","throw2","throw3","holster","draw"};
    if(model.resource_name!="models/v_grenade.mdl" || model.sequences.size()!=labels.size()) return false;
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& sequence=model.sequences[i];
        if(sequence.label!=labels[i] || !sequence.frame_count || !std::isfinite(sequence.fps) || sequence.fps<=0) return false;
    }
    return model.supported_bodies[0];
}
enum class HandGrenadePhase : std::uint8_t { ready,primed,release_pending,throwing,post_throw,retired };
struct HandGrenadeThrowContext {
    game_api::LocalWeaponSubmittedCommand::ShotContext input;
    std::array<double,3> adjusted_angles{};
    std::optional<std::array<double,3>> reference_velocity;
    double speed{},remaining_fuse{};
    std::uint32_t sequence{};
};
inline std::optional<HandGrenadeThrowContext> hand_grenade_throw_context(
    const game_api::LocalWeaponSubmittedCommand::ShotContext& input,double elapsed) noexcept {
    if(!input.view_angles_degrees || !input.punch_angles_degrees || !std::isfinite(elapsed) || elapsed<0) return {};
    HandGrenadeThrowContext result;result.input=input;
    for(std::size_t i=0;i<3;++i) {
        if(!std::isfinite((*input.view_angles_degrees)[i]) || !std::isfinite((*input.punch_angles_degrees)[i])) return {};
        result.adjusted_angles[i]=(*input.view_angles_degrees)[i]+(*input.punch_angles_degrees)[i];
    }
    const auto pitch=result.adjusted_angles[0];
    result.adjusted_angles[0]=-10+pitch*(pitch<0 ? 80.0/90.0 : 100.0/90.0);
    result.speed=std::min(1000.0,(90-result.adjusted_angles[0])*6.5);
    if(!std::isfinite(result.speed)) return {};
    result.sequence=result.speed<500 ? 3U : result.speed<1000 ? 4U : 5U;
    result.remaining_fuse=std::max(0.0,3.0-elapsed);
    if(input.player_velocity) {
        std::array<double,3> velocity{};
        for(std::size_t i=0;i<3;++i) if(!std::isfinite((*input.player_velocity)[i])) return {};
        constexpr auto radians=3.14159265358979323846/180.0;
        const auto p=result.adjusted_angles[0]*radians,y=result.adjusted_angles[1]*radians;
        velocity[0]=std::cos(p)*std::cos(y)*result.speed+(*input.player_velocity)[0];
        velocity[1]=std::cos(p)*std::sin(y)*result.speed+(*input.player_velocity)[1];
        velocity[2]=-std::sin(p)*result.speed+(*input.player_velocity)[2];
        result.reference_velocity=velocity;
    }
    return result;
}
struct HandGrenadePresentationState {
    HandGrenadePhase phase{HandGrenadePhase::ready};
    std::optional<double> prime_time,release_time,throw_time;
    std::optional<double> server_start_throw,server_release_throw;
    std::optional<HandGrenadeThrowContext> frozen_throw;
    std::uint32_t prime_command{},release_command{},throw_command{};
    std::size_t prime_count{},throw_count{},corrections{};
    // A presentation-only reservation. Canonical ammo/HUD is never decremented.
    std::uint8_t reserved_ammo{};
};
}
