#pragma once
#include <hlclient/game_api/presentation.hpp>
#include <array>
#include <cmath>
#include <string_view>
namespace hlclient::games::halflife {
// Pinned Valve SDK b1b5cf5892918535619b2937bb927e46cb097ba1.
// Reference constants describe presentation only; HLDS owns all world charges.
inline constexpr std::uint8_t satchel_maximum_ammo=5, satchel_default_give=1;
inline constexpr double satchel_throw_speed=274, satchel_primary_cycle=1.0,
    satchel_secondary_cycle=.5;
inline constexpr std::array<std::string_view,2> satchel_viewmodel_names{
    "models/v_satchel.mdl","models/v_satchel_radio.mdl"};
inline bool satchel_command(std::string_view name) noexcept { return name=="weapon_satchel"; }
inline std::optional<bool> satchel_model_role(std::string_view name) noexcept {
    if(name==satchel_viewmodel_names[0]) return false;
    if(name==satchel_viewmodel_names[1]) return true;
    return {};
}
inline bool satchel_sequences(const game_api::LocalWeaponModelMetadata& model) noexcept {
    const auto radio=satchel_model_role(model.resource_name);if(!radio) return false;
    constexpr std::array<std::string_view,5> labels{"idle1","fidget1","draw","fire","holster"};
    if(model.sequences.size()!=(*radio ? 5U : 4U) || !model.supported_bodies[0]) return false;
    for(std::size_t i=0;i<model.sequences.size();++i) {
        const auto& s=model.sequences[i];
        const auto label=!(*radio) && i==3U ? std::string_view{"drop"} : labels[i];
        if(s.label!=label || !s.frame_count || !std::isfinite(s.fps) || s.fps<=0) return false;
    }
    return true;
}
struct SatchelPresentationState {
    std::optional<std::int32_t> charge_ready,server_charge_ready;
    bool retired{};
    std::uint8_t reserved_ammo{};
    std::size_t throws{},detonations{},corrections{};
    std::optional<double> detonation_time;
    // Owning immutable input for reference diagnostics, never a world entity.
    std::optional<game_api::LocalWeaponSubmittedCommand::ShotContext> last_throw_context;
};
}
