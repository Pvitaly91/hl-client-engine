#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <string_view>

namespace hlclient::entity_render {

struct SpriteBillboardInput {
    assets::SpriteOrientation orientation{
        assets::SpriteOrientation::view_parallel};
    assets::AssetVector3 camera_forward{};
    assets::AssetVector3 camera_right{};
    assets::AssetVector3 camera_up{};
    // Oriented sprites consume a caller-projected, protocol-neutral entity
    // basis. This module does not guess a wire-format Euler convention.
    assets::AssetVector3 oriented_forward{};
    assets::AssetVector3 oriented_right{};
    assets::AssetVector3 oriented_up{};
};

enum class SpriteBillboardBasisStatus {
    supported_view_parallel,
    supported_view_parallel_upright,
    supported_oriented,
    unsupported_facing_upright_evidence_pending,
    unsupported_view_parallel_oriented_evidence_pending,
    non_finite_input,
    degenerate_camera_basis,
    degenerate_upright_basis,
    degenerate_oriented_basis,
};

[[nodiscard]] std::string_view to_string(
    SpriteBillboardBasisStatus status) noexcept;

struct SpriteBillboardBasisResult {
    assets::AssetVector3 right{};
    assets::AssetVector3 up{};
    // Points toward the viewer for supported view-parallel profiles.
    assets::AssetVector3 normal{};
    SpriteBillboardBasisStatus status{
        SpriteBillboardBasisStatus::degenerate_camera_basis};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return status == SpriteBillboardBasisStatus::supported_view_parallel ||
            status == SpriteBillboardBasisStatus::supported_view_parallel_upright ||
            status == SpriteBillboardBasisStatus::supported_oriented;
    }
};

class SpriteBillboardBasis final {
public:
    [[nodiscard]] static SpriteBillboardBasisResult calculate(
        const SpriteBillboardInput& input) noexcept;
};

} // namespace hlclient::entity_render
