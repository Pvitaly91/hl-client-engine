#pragma once
#include <hlclient/game_api/beam_effects.hpp>
#include <cmath>
#include <string_view>
namespace hlclient::games::halflife {
inline constexpr std::uint8_t snark_weapon_id=15, snark_maximum_ammo=15, snark_default_give=5;
inline constexpr double snark_throw_cycle=.3, snark_recovery_seconds=1.0;
inline constexpr std::uint32_t snark_up_sequence=4, snark_throw_sequence=5, snark_run_sequence=3;
inline bool snark_command(std::string_view name) noexcept {return name=="weapon_snark";}
// Sequence roles come from the pinned SDK enum. Actual imported frame/FPS/body
// metadata, not a synthetic duration, controls the displayed Studio pose.
inline bool snark_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    if(model.resource_name!="models/v_squeak.mdl" || model.sequences.size()!=6 ||
        !model.supported_bodies[0] || !model.selectable_bodies[0]) return false;
    constexpr std::array<std::string_view,6> labels{"idle1","fidgetfit","fidgetnip","down","up","throw"};
    for(std::size_t i=0;i<labels.size();++i) {
        const auto& sequence=model.sequences[i];
        if(sequence.label!=labels[i] || !sequence.frame_count || !std::isfinite(sequence.fps) ||
            sequence.fps<=0 || sequence.fps>1000) return false;
    }
    return true;
}
struct SnarkPresentationState {
    std::uint64_t attempts{},eligible{},blocked{},unavailable{},duplicates{},confirmed{},rejected{};
    std::uint8_t reserved_ammo{};
    bool retired{};
    std::optional<game_api::PlacementTraceRequest> last_trace;
    std::optional<game_api::PlacementTraceResult> last_result;
};
}
