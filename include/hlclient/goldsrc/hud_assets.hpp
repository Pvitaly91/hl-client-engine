#pragma once
#include <hlclient/entity_render/sprite_render_asset.hpp>
#include <hlclient/filesystem/file_system.hpp>
#include <hlclient/game_api/hud_resources.hpp>
#include <hlclient/game_api/text_messages.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace hlclient::goldsrc {
inline constexpr std::size_t kMaximumHudSpriteSourceBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t kMaximumHudAggregateSourceBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaximumHudDecodedBytes = 160U * 1024U * 1024U;
struct HudPreparedSprite final {
    std::uint64_t resource_id{}, resource_revision{};
    std::shared_ptr<const entity_render::SpriteRenderAsset> asset;
};
struct HudPreparedAssets final {
    game_api::HudResourceSet resources;
    game_api::TextMessageCatalogue text_messages;
    game_api::HudResourceStatus text_messages_status{game_api::HudResourceStatus::missing_file};
    std::vector<HudPreparedSprite> sprites;
    std::size_t source_bytes{}, retained_decoded_bytes{}, ready_entries{}, rejected_entries{};
};
// Filesystem-free policy composition used with project-owned fixtures and the
// exact-root approved reader below. AssetManager performs canonical SPR import;
// the descriptor parser merely authorizes a bounded dependency set.
[[nodiscard]] HudPreparedAssets prepare_hud_assets(
    const filesystem::IFileSystem& approved_source,
    std::span<const game_api::HudManifestRequest>, std::stop_token = {});

// One cancellable startup worker, no file IO in HUD frames/audio callbacks.
// The only production reader is bound to the selected approved world's root.
class ApprovedHudAssets final {
public:
    ApprovedHudAssets(PreparedSoundResources, std::span<const game_api::HudManifestRequest>,
        std::string_view text_resource = {});
    ~ApprovedHudAssets();
    ApprovedHudAssets(const ApprovedHudAssets&) = delete;
    ApprovedHudAssets& operator=(const ApprovedHudAssets&) = delete;
    [[nodiscard]] std::shared_ptr<const HudPreparedAssets> snapshot() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hlclient::goldsrc
