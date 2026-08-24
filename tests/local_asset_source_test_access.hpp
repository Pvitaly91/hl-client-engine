#pragma once

#include <hlclient/local_assets/local_asset_source.hpp>

namespace hlclient::local_assets::detail {

class LocalAssetSourceOpenOperationTestAccess final {
public:
    [[nodiscard]] static local_resources::LocalReadOnlyFile* file(
        LocalAssetSourceOpenOperation& operation) noexcept
    {
        return operation.file_ ? &*operation.file_ : nullptr;
    }

    static void simulate_final_change_metadata(
        LocalAssetSourceOpenOperation& operation) noexcept
    {
        operation.initial_snapshot_.change_time ^= 1;
    }

    static void simulate_final_identity_replacement(
        LocalAssetSourceOpenOperation& operation) noexcept
    {
        operation.initial_snapshot_.identity =
            local_resources::LocalStableFileIdentity{};
    }
};

} // namespace hlclient::local_assets::detail
