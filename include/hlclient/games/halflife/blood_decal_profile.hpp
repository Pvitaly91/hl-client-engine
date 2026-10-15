#pragma once
#include <hlclient/game_api/surface_decals.hpp>
#include <array>
#include <optional>
#include <string_view>

namespace hlclient::games::halflife {
inline constexpr std::array<std::string_view,12> blood_decal_names{
    "{blood1","{blood2","{blood3","{blood4","{blood5","{blood6",
    "{yblood1","{yblood2","{yblood3","{yblood4","{yblood5","{yblood6"};
inline constexpr auto blood_decal_assets=[] {
    std::array<game_api::DecalAssetRequest,12> result{};
    constexpr std::string_view archive="decals.wad";
    for(std::size_t i=0;i<result.size();++i) {
        for(std::size_t j=0;j<archive.size();++j)result[i].wad_name[j]=archive[j];
        for(std::size_t j=0;j<blood_decal_names[i].size();++j)
            result[i].texture_name[j]=blood_decal_names[i][j];
        // Explicit per-dependency interpretation; verified actual-asset audit
        // will select this profile, never a global '{' texture color-key.
        result[i].interpretation=game_api::DecalImageInterpretation::palette_tint_index_coverage;
        result[i].material_mode=game_api::LocalDecalMaterialMode::lit_straight_alpha;
    }
    return result;
}();
[[nodiscard]] inline std::optional<std::uint8_t> blood_decal_asset_slot(std::string_view name) noexcept {
    for(std::size_t i=0;i<blood_decal_names.size();++i) {
        const auto expected=blood_decal_names[i];
        if(name.size()!=expected.size())continue;
        bool equal=true;
        for(std::size_t c=0;c<name.size();++c) {
            const auto value=name[c]>='A' && name[c]<='Z' ?
                static_cast<char>(name[c]-'A'+'a') : name[c];
            equal=equal && value==expected[c];
        }
        if(equal)return static_cast<std::uint8_t>(i);
    }
    return {};
}
} // namespace hlclient::games::halflife
