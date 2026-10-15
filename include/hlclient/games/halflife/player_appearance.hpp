#pragma once
#include <hlclient/game_api/multiplayer.hpp>
namespace hlclient::games::halflife {
[[nodiscard]] game_api::PlayerAppearanceSelection select_player_appearance(
    const game_api::MultiplayerHudState&,std::uint32_t,std::string_view);
[[nodiscard]] std::vector<game_api::PlayerPaletteRange> player_palette_ranges(
    const game_api::PlayerAppearanceFields&,const assets::SkeletalModelAssetData&);
}
