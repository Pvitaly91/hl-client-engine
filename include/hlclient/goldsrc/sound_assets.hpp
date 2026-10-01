#pragma once
#include <hlclient/goldsrc/server_audio.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
namespace hlclient::goldsrc {
struct PreparedSoundResources {
    std::shared_ptr<const PrecacheManifestState> manifest;
    std::shared_ptr<const local_resources::LocalResourceEnvironment> environment;
};
// One cancellable loader, max 512 exact sparse bindings, 32 MiB PCM cache.
// No cache eviction: active/pending shared PCM cannot become dangling.
class ApprovedSoundAssets final : public SoundAssets {
public:
    explicit ApprovedSoundAssets(PreparedSoundResources);
    ~ApprovedSoundAssets() override;
    SoundAssetResult request(std::uint16_t) noexcept override;
    std::string_view virtual_name(std::uint16_t) const noexcept override;
    SoundAssetResult request_local(const game_api::LocalSoundReference&) noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
