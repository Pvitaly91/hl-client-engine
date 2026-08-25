#pragma once

#include <hlclient/goldsrc/connect_request_stage.hpp>

namespace hlclient::goldsrc::detail {

class GoldSrcHandshakeCoordinatorTestAccess final {
public:
    [[nodiscard]] static ResourceClientResponseStage* resource_response_stage(
        GoldSrcHandshakeCoordinator& coordinator) noexcept
    {
        return coordinator.resource_client_response_stage_
            ? &*coordinator.resource_client_response_stage_
            : nullptr;
    }

    [[nodiscard]] static PostResourceEntitySnapshotStage* post_resource_stage(
        GoldSrcHandshakeCoordinator& coordinator) noexcept
    {
        return coordinator.post_resource_entity_snapshot_stage_.get();
    }

    static void synchronize_from_post_resource_stage(
        GoldSrcHandshakeCoordinator& coordinator)
    {
        coordinator.synchronize_from_post_resource_entity_snapshot();
    }

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
