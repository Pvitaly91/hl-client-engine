#include <hlclient/games/halflife/hud_resource_profile.hpp>
#include <array>
#include <utility>
namespace hlclient::games::halflife {
namespace {
constexpr std::array<std::pair<std::string_view, std::string_view>, 14U> descriptions{{
    {"weapon_crowbar", "sprites/weapon_crowbar.txt"},
    {"weapon_9mmhandgun", "sprites/weapon_9mmhandgun.txt"},
    {"weapon_357", "sprites/weapon_357.txt"},
    {"weapon_9mmAR", "sprites/weapon_9mmAR.txt"},
    {"weapon_crossbow", "sprites/weapon_crossbow.txt"},
    {"weapon_shotgun", "sprites/weapon_shotgun.txt"},
    {"weapon_rpg", "sprites/weapon_rpg.txt"},
    {"weapon_gauss", "sprites/weapon_gauss.txt"},
    {"weapon_egon", "sprites/weapon_egon.txt"},
    {"weapon_hornetgun", "sprites/weapon_hornetgun.txt"},
    {"weapon_handgrenade", "sprites/weapon_handgrenade.txt"},
    {"weapon_tripmine", "sprites/weapon_tripmine.txt"},
    {"weapon_satchel", "sprites/weapon_satchel.txt"},
    {"weapon_snark", "sprites/weapon_snark.txt"}
}};
}
std::string_view halflife_hud_descriptor_name(std::string_view name) noexcept {
    // Pinned base-HL entity aliases; never derive a descriptor from v_model.
    if (name == "weapon_glock") name = "weapon_9mmhandgun";
    else if (name == "weapon_python") name = "weapon_357";
    else if (name == "weapon_mp5") name = "weapon_9mmAR";
    for (const auto& [command, descriptor] : descriptions) if (command == name) return descriptor;
    return {};
}
std::span<const game_api::HudManifestRequest> halflife_hud_resource_preparation() {
    static const auto requests = [] {
        std::vector<game_api::HudManifestRequest> out;
        out.reserve(15U);
        out.push_back({"sprites/hud.txt", game_api::HudManifestCountCompatibility{
            "E7922C3D3ED2EC59614E032D1C5A8194AA4ABED7713193191EE1E68A2FF39EF5", 257U, 247U}});
        for (const auto& [command, descriptor] : descriptions) {
            (void)command; out.push_back({std::string{descriptor}, std::nullopt});
        }
        return out;
    }();
    return requests;
}
} // namespace hlclient::games::halflife
