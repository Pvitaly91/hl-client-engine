#pragma once

#include <hlclient/assets/asset_importer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace hlclient::tests::synthetic_assets {

namespace assets = hlclient::assets;

inline constexpr std::array<std::byte, 8U> kWorldSignature{
    std::byte{'S'}, std::byte{'Y'}, std::byte{'N'}, std::byte{'W'},
    std::byte{'O'}, std::byte{'R'}, std::byte{'L'}, std::byte{'D'},
};
inline constexpr std::array<std::byte, 8U> kModelSignature{
    std::byte{'S'}, std::byte{'Y'}, std::byte{'N'}, std::byte{'M'},
    std::byte{'O'}, std::byte{'D'}, std::byte{'E'}, std::byte{'L'},
};
inline constexpr std::array<std::byte, 8U> kSpriteSignature{
    std::byte{'S'}, std::byte{'Y'}, std::byte{'N'}, std::byte{'S'},
    std::byte{'P'}, std::byte{'R'}, std::byte{'I'}, std::byte{'T'},
};
inline constexpr std::array<std::byte, 8U> kAudioSignature{
    std::byte{'S'}, std::byte{'Y'}, std::byte{'N'}, std::byte{'A'},
    std::byte{'U'}, std::byte{'D'}, std::byte{'I'}, std::byte{'O'},
};
inline constexpr std::byte kSyntheticVersion{1U};

struct SyntheticImporterCounts {
  std::size_t probe_count{0U};
  std::size_t import_count{0U};
};

enum class SyntheticImportBehavior {
  success,
  malformed_data,
  import_failure,
  standard_exception,
  unknown_exception,
};

template <class Asset>
[[nodiscard]] constexpr const std::array<std::byte, 8U> &
signature_for() noexcept {
  if constexpr (std::is_same_v<Asset, assets::WorldAsset>) {
    return kWorldSignature;
  } else if constexpr (std::is_same_v<Asset, assets::ModelAsset>) {
    return kModelSignature;
  } else if constexpr (std::is_same_v<Asset, assets::SpriteAsset>) {
    return kSpriteSignature;
  } else {
    static_assert(std::is_same_v<Asset, assets::AudioAsset>);
    return kAudioSignature;
  }
}

template <class Asset>
[[nodiscard]] std::vector<std::byte>
source_bytes(const std::size_t byte_count = 32U) {
  const auto minimum_size = signature_for<Asset>().size() + 1U;
  std::vector<std::byte> result((std::max)(byte_count, minimum_size));
  std::ranges::copy(signature_for<Asset>(), result.begin());
  result[signature_for<Asset>().size()] = kSyntheticVersion;
  for (std::size_t index = minimum_size; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(index & 0xffU);
  }
  return result;
}

template <class Asset>
[[nodiscard]] bool
structurally_matches(const assets::AssetSource &source) noexcept {
  const auto bytes = source.bytes();
  const auto &signature = signature_for<Asset>();
  return bytes.size() > signature.size() &&
         std::equal(signature.begin(), signature.end(), bytes.begin()) &&
         bytes[signature.size()] == kSyntheticVersion;
}

template <class Asset>
[[nodiscard]] Asset make_neutral_asset(const std::string_view importer_id) {
  Asset result;
  result.identity.source_name.assign(importer_id);
  if constexpr (std::is_same_v<Asset, assets::WorldAsset>) {
    result.vertices.resize(3U);
    result.indices = {0U, 1U, 2U};
    result.surfaces.push_back(assets::WorldSurface{0U, 3U, 0U});
  } else if constexpr (std::is_same_v<Asset, assets::ModelAsset>) {
    result.vertices.resize(3U);
    result.indices = {0U, 1U, 2U};
  } else if constexpr (std::is_same_v<Asset, assets::SpriteAsset>) {
    assets::ImageAsset image;
    image.identity.source_name.assign(importer_id);
    image.width = 1U;
    image.height = 1U;
    image.pixels = {
        std::byte{0xffU},
        std::byte{0xffU},
        std::byte{0xffU},
        std::byte{0xffU},
    };
    result.frames.push_back(assets::SpriteFrame{std::move(image), 0.1F});
  } else {
    static_assert(std::is_same_v<Asset, assets::AudioAsset>);
    result.sample_rate = 22'050U;
    result.channel_count = 1U;
    result.interleaved_samples = {0.0F};
  }
  return result;
}

template <class Asset>
class SyntheticAssetImporter : public assets::IAssetImporter<Asset> {
public:
  SyntheticAssetImporter(
      std::string importer_id, SyntheticImporterCounts &counts,
      const assets::AssetProbeConfidence confidence = 100U,
      const SyntheticImportBehavior behavior = SyntheticImportBehavior::success,
      std::function<void()> probe_callback = {},
      std::function<void()> import_callback = {})
      : importer_id_{std::move(importer_id)}, counts_{counts},
        confidence_{confidence}, behavior_{behavior},
        probe_callback_{std::move(probe_callback)},
        import_callback_{std::move(import_callback)} {}

  [[nodiscard]] std::string_view id() const noexcept override {
    return importer_id_;
  }

  [[nodiscard]] assets::AssetProbeConfidence
  probe(const assets::AssetProbe &probe) const noexcept override {
    if (probe_callback_) {
      try {
        probe_callback_();
      } catch (...) {
      }
    }
    ++counts_.probe_count;
    const auto &signature = signature_for<Asset>();
    if (probe.signature.size() < signature.size() ||
        probe.structural_bytes.size() <= signature.size() ||
        !std::equal(signature.begin(), signature.end(),
                    probe.signature.begin()) ||
        probe.structural_bytes[signature.size()] != kSyntheticVersion) {
      return assets::kAssetProbeNoMatch;
    }
    return confidence_;
  }

  [[nodiscard]] assets::AssetResult<Asset>
  import(const assets::AssetSource &source) const override {
    if (import_callback_) {
      import_callback_();
    }
    ++counts_.import_count;
    switch (behavior_) {
    case SyntheticImportBehavior::success:
      if (!structurally_matches<Asset>(source)) {
        return assets::AssetResult<Asset>::failure(assets::AssetError{
            assets::AssetErrorCode::MalformedData,
            {},
            {},
            "Synthetic structural source is invalid",
            {},
        });
      }
      return assets::AssetResult<Asset>::success(
          make_neutral_asset<Asset>(importer_id_));
    case SyntheticImportBehavior::malformed_data:
      return assets::AssetResult<Asset>::failure(assets::AssetError{
          assets::AssetErrorCode::MalformedData,
          {},
          {},
          "Synthetic importer reported malformed data",
          {},
      });
    case SyntheticImportBehavior::import_failure:
      return assets::AssetResult<Asset>::failure(assets::AssetError{
          assets::AssetErrorCode::ImportFailed,
          {},
          {},
          "Synthetic importer failed",
          {},
      });
    case SyntheticImportBehavior::standard_exception:
      throw std::runtime_error{"Synthetic importer exception"};
    case SyntheticImportBehavior::unknown_exception:
      throw 42;
    }
    throw std::runtime_error{"Invalid synthetic importer behavior"};
  }

private:
  std::string importer_id_;
  SyntheticImporterCounts &counts_;
  assets::AssetProbeConfidence confidence_{assets::kAssetProbeNoMatch};
  SyntheticImportBehavior behavior_{SyntheticImportBehavior::success};
  std::function<void()> probe_callback_;
  std::function<void()> import_callback_;
};

using SyntheticWorldImporter = SyntheticAssetImporter<assets::WorldAsset>;
using SyntheticModelImporter = SyntheticAssetImporter<assets::ModelAsset>;
using SyntheticSpriteImporter = SyntheticAssetImporter<assets::SpriteAsset>;
using SyntheticAudioImporter = SyntheticAssetImporter<assets::AudioAsset>;

} // namespace hlclient::tests::synthetic_assets
