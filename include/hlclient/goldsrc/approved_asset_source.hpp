#pragma once

#include <hlclient/assets/asset_importer_dispatcher.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>

#include <cstddef>
#include <cstdint>

namespace hlclient::goldsrc {

class AssetDispatchPlan;
class ApprovedAssetImporterDispatcher;
class ApprovedAssetSourceOpenOperation;
struct ApprovedAssetSourceCreateResult;

// Network-free, owning capability shared by the manifest dispatch stage and
// offline visual-asset composition. Construction remains restricted to the
// manifest-approved opener; read-only access does not require linking the
// sign-on/resource-stage implementation.
class ApprovedAssetSource final {
public:
    ApprovedAssetSource(ApprovedAssetSource&&) noexcept = default;
    ApprovedAssetSource& operator=(ApprovedAssetSource&&) noexcept = delete;
    ApprovedAssetSource(const ApprovedAssetSource&) = delete;
    ApprovedAssetSource& operator=(const ApprovedAssetSource&) = delete;
    ~ApprovedAssetSource() = default;

    [[nodiscard]] const assets::AssetSource& source() const noexcept
    {
        return source_.source();
    }

    [[nodiscard]] std::size_t wire_ordinal() const noexcept
    {
        return wire_ordinal_;
    }

    [[nodiscard]] ResourceType resource_type() const noexcept
    {
        return resource_type_;
    }

    [[nodiscard]] std::uint16_t resource_index() const noexcept
    {
        return resource_index_;
    }

    [[nodiscard]] local_resources::LocalResourceRootId root_id() const noexcept
    {
        return source_.root_id();
    }

    [[nodiscard]] local_resources::LocalVirtualResourceId virtual_resource_id()
        const noexcept
    {
        return source_.virtual_resource_id();
    }

    [[nodiscard]] local_resources::LocalStableFileIdentity expected_identity()
        const noexcept
    {
        return source_.expected_identity();
    }

    [[nodiscard]] std::uint64_t byte_count() const noexcept
    {
        return source_.byte_count();
    }

    [[nodiscard]] assets::AssetDispatchRole role() const noexcept
    {
        return role_;
    }

    [[nodiscard]] PrecacheManifestCompatibilityProfile compatibility_profile()
        const noexcept
    {
        return compatibility_profile_;
    }

    [[nodiscard]] PrecacheManifestEvidenceProfile evidence_profile()
        const noexcept
    {
        return evidence_profile_;
    }

private:
    friend class ApprovedAssetImporterDispatcher;
    friend class ApprovedAssetSourceOpenOperation;

    [[nodiscard]] static ApprovedAssetSourceCreateResult create(
        const AssetDispatchPlan& plan,
        local_assets::LocalAssetSource source) noexcept;

    ApprovedAssetSource(
        local_assets::LocalAssetSource source,
        std::size_t wire_ordinal,
        ResourceType resource_type,
        std::uint16_t resource_index,
        assets::AssetDispatchRole role,
        PrecacheManifestCompatibilityProfile compatibility_profile,
        PrecacheManifestEvidenceProfile evidence_profile) noexcept;

    local_assets::LocalAssetSource source_;
    std::size_t wire_ordinal_{0U};
    ResourceType resource_type_{ResourceType::sound};
    std::uint16_t resource_index_{0U};
    assets::AssetDispatchRole role_{assets::AssetDispatchRole::unsupported};
    PrecacheManifestCompatibilityProfile compatibility_profile_{
        PrecacheManifestCompatibilityProfile::
            stock_protocol_48_standard_metadata_only};
    PrecacheManifestEvidenceProfile evidence_profile_{
        PrecacheManifestEvidenceProfile::
            exact_correlated_local_resource_metadata};
};

} // namespace hlclient::goldsrc
