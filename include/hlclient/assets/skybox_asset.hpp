#pragma once
#include <hlclient/assets/asset_types.hpp>
#include <array>

namespace hlclient::assets {
// Owning immutable environment, in OpenGL cube order (+X,-X,+Y,-Y,+Z,-Z).
// Cube coordinates are (world.x, world.z, world.y); RGBA rows are already
// oriented for cube sampling. No source names or filesystem access in renderers.
struct SkyboxAsset {
    std::array<ImageAsset,6> faces;
};
}
