#pragma once
#include <hlclient/assets/asset_importer.hpp>
namespace hlclient::assets {
// Pure, bounded RIFF PCM decoder; no filesystem/device dependencies.
class WavImporter final : public IAudioImporter {
public:
    std::string_view id() const noexcept override { return "pcm-riff-wave-v1"; }
    AssetProbeConfidence probe(const AssetProbe&) const noexcept override;
    AudioAssetResult import(const AssetSource&) const override;
};
}
