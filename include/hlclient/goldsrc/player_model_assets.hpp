#pragma once
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/game_api/player_appearance.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <memory>
#include <stop_token>

namespace hlclient::goldsrc {
inline constexpr std::size_t kMaximumPlayerModelRequests=64U;
struct PreparedPlayerModel final {
    game_api::PlayerAppearanceStatus status{game_api::PlayerAppearanceStatus::asset_pending};
    std::shared_ptr<const assets::ModelAsset> model;
    assets::AssetSourceFingerprint revision;
    std::uint64_t source_bytes{};
};
[[nodiscard]] bool valid_player_model_virtual_name(std::string_view) noexcept;
[[nodiscard]] std::uint64_t retained_player_model_bytes(const assets::ModelAsset&) noexcept;
[[nodiscard]] std::shared_ptr<const assets::ModelAsset> remap_player_model_palette(
    const assets::ModelAsset&,std::span<const game_api::PlayerPaletteRange>);
// Same canonical verified-source/bundle importer as server Studio resources,
// but deliberately no server resource index. Useful for approved fixture tests.
[[nodiscard]] PreparedPlayerModel import_player_model_source(
    const local_assets::LocalAssetSource&,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>,std::stop_token = {});
class ApprovedPlayerModelAssets final {
public:
    explicit ApprovedPlayerModelAssets(PreparedSoundResources);
    ~ApprovedPlayerModelAssets();
    ApprovedPlayerModelAssets(const ApprovedPlayerModelAssets&)=delete;
    ApprovedPlayerModelAssets& operator=(const ApprovedPlayerModelAssets&)=delete;
    // Enqueues at most once. Nonblocking and no file IO on the render thread.
    [[nodiscard]] PreparedPlayerModel request(std::string_view) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
