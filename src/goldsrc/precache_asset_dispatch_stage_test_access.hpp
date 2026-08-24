#pragma once

#include <hlclient/goldsrc/precache_asset_dispatch_stage.hpp>

namespace hlclient::goldsrc::detail {

class PrecacheAssetDispatchStageTestAccess final {
public:
    [[nodiscard]] static local_assets::LocalAssetSourceOpenOperation*
    source_open_operation(PrecacheAssetDispatchStage& stage) noexcept;
};

} // namespace hlclient::goldsrc::detail
