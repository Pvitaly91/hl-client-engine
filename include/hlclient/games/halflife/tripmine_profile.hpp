#pragma once
#include <hlclient/game_api/beam_effects.hpp>
#include <array>
#include <cmath>
#include <string_view>
namespace hlclient::games::halflife {
inline constexpr std::uint8_t tripmine_weapon_id=13, tripmine_maximum_ammo=5,
    tripmine_default_give=1;
inline constexpr double tripmine_attempt_cycle=.3, tripmine_trace_distance=128;
inline constexpr std::uint32_t tripmine_draw_sequence=6,tripmine_world_sequence=7,
    tripmine_ground_sequence=8,tripmine_world_body=3;
inline bool tripmine_command(std::string_view name) noexcept {return name=="weapon_tripmine";}
inline bool tripmine_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    constexpr std::array<std::string_view,9> names{
        "idle1","idle2","arm1","place","fidget","holster","draw","world","ground"};
    if(model.resource_name!="models/v_tripmine.mdl" || model.sequences.size()!=names.size() ||
        !model.supported_bodies[0] || !model.selectable_bodies[3]) return false;
    for(std::size_t i=0;i<names.size();++i) {
        const auto& s=model.sequences[i];
        if(s.label!=names[i] || !s.frame_count || !std::isfinite(s.fps) || s.fps<0 ||
            (s.frame_count>1 && s.fps<=0))return false;
    }return model.sequences[6].fps>0;
}
struct TripminePresentationState {
    std::uint64_t attempts{},eligible{},no_hit{},unavailable{},duplicates{},confirmed{},rejected{};
    std::uint8_t reserved_ammo{};
    bool retired{};
    std::optional<game_api::PlacementTraceRequest> last_trace;
    std::optional<game_api::PlacementTraceResult> last_result;
};
}
