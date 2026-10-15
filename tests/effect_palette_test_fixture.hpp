#pragma once
#include <hlclient/game_api/effect_palette.hpp>
#include <cstddef>
#include <vector>

namespace hlclient::tests::effect_palette_fixture {
// Independently authored palette, deliberately different from installed HL.
// The tests must prove lookup, rather than succeed on hardcoded stock RGB.
inline game_api::EffectPaletteMetadata metadata() {
    game_api::EffectPaletteMetadata result;result.status=game_api::EffectPaletteStatus::ready;
    result.colors.fill({40,40,40});result.colors[195]={160,130,20};result.colors[247]={80,0,0};
    return result;
}
inline std::vector<std::byte> bytes() {
    const auto palette=metadata();std::vector<std::byte> result;result.reserve(768);
    for(const auto& color:palette.colors)for(auto channel:color)result.push_back(static_cast<std::byte>(channel));
    return result;
}
} // namespace hlclient::tests::effect_palette_fixture
