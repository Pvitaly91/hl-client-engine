#pragma once
#include <hlclient/assets/skybox_asset.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <span>
namespace hlclient::goldsrc {
enum class SkyAssetStatus { not_requested, ready, invalid_name, missing, rejected };
[[nodiscard]] constexpr std::string_view to_string(SkyAssetStatus status) noexcept {
    switch(status) {
    case SkyAssetStatus::not_requested:return "not_requested";
    case SkyAssetStatus::ready:return "ready";
    case SkyAssetStatus::invalid_name:return "invalid_name";
    case SkyAssetStatus::missing:return "missing";
    case SkyAssetStatus::rejected:return "rejected";
    }
    return "rejected";
}
struct PreparedSkyAssets {
    SkyAssetStatus status{SkyAssetStatus::not_requested};
    std::shared_ptr<const assets::SkyboxAsset> skybox;
};
[[nodiscard]] PreparedSkyAssets prepare_sky_assets(const PreparedSoundResources&,
    std::string_view server_sky_name,std::span<const std::byte> world_entity_lump);
}
