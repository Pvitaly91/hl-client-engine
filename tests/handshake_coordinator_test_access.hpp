#pragma once

#include <hlclient/goldsrc/connect_request_stage.hpp>

namespace hlclient::goldsrc::detail {

class GoldSrcHandshakeCoordinatorTestAccess final {
public:
    [[nodiscard]] static PrecacheAssetDispatchStage* asset_dispatch_stage(
        GoldSrcHandshakeCoordinator& coordinator) noexcept
    {
        return coordinator.asset_dispatch_stage_.get();
    }

    [[nodiscard]] static WorldRenderPackageStage* world_render_package_stage(
        GoldSrcHandshakeCoordinator& coordinator) noexcept
    {
        return coordinator.world_render_package_stage_.get();
    }
};

} // namespace hlclient::goldsrc::detail
