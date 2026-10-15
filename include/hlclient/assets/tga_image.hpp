#pragma once
#include <hlclient/assets/asset_types.hpp>
#include <span>
#include <optional>
namespace hlclient::assets {
// Bounded truecolor TGA profile, uncompressed/RLE, 24/32-bit. Returns RGBA8,
// top-left rows; descriptor orientation is applied exactly once. No file I/O.
[[nodiscard]] std::optional<ImageAsset> decode_tga_image(std::span<const std::byte>);
}
