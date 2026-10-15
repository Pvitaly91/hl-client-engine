#include <hlclient/games/halflife/player_appearance.hpp>
#include <algorithm>

namespace hlclient::games::halflife {
game_api::PlayerAppearanceSelection select_player_appearance(
    const game_api::MultiplayerHudState& directory,std::uint32_t slot,std::string_view network_name) {
    using S=game_api::PlayerAppearanceStatus;
    game_api::PlayerAppearanceSelection result;
    if(!slot || slot>directory.maximum_clients || slot>directory.players.size()) return result;
    const auto& player=directory.players[slot-1U];
    if(!player.identity_observed || !player.present) return result;
    result.fields=player.appearance;
    result.occupant_epoch=player.occupant_epoch;result.appearance_revision=player.appearance_revision;
    // SetupPlayerModel is a player appearance policy, not canonical entity
    // mutation. This bounded HL profile remaps only the normal base player
    // resource; explicitly selected server Studio models retain precedence.
    if(network_name!="models/player.mdl") {result.status=S::explicit_server_model;return result;}
    const auto& fields=result.fields;
    if(!fields.model_advertised) return result;
    if(fields.model_token_rejected || fields.model.empty() || fields.model.size()>32U ||
       !std::all_of(fields.model.begin(),fields.model.end(),[](unsigned char c){
           return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-';
       })) {result.status=S::model_token_rejected;return result;}
    if(fields.model=="player") {result.status=S::default_model_requested;return result;}
    result.model_name="models/player/"+fields.model+"/"+fields.model+".mdl";
    result.status=S::asset_pending;
    return result;
}
std::vector<game_api::PlayerPaletteRange> player_palette_ranges(
    const game_api::PlayerAppearanceFields& fields,const assets::SkeletalModelAssetData& model) {
    std::vector<game_api::PlayerPaletteRange> result;
    if(model.textures.size()>128U) return result;
    for(std::size_t i=0;i<model.textures.size();++i) {
        auto name=model.textures[i].source_name;
        for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
        // Narrow original DM_Base indexed palette contract. Other textures
        // remain exact; this is never a whole-model RGB multiplier.
        if(name!="dm_base.bmp" && name!="dm_base")continue;
        if(fields.top_color)result.push_back({static_cast<std::uint32_t>(i),160U,191U,*fields.top_color});
        if(fields.bottom_color)result.push_back({static_cast<std::uint32_t>(i),192U,223U,*fields.bottom_color});
    }
    return result;
}
}
