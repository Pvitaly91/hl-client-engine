#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hlclient::assets {

struct AssetIdentity {
    std::string source_name;
};

struct AssetVector2 {
    float x{0.0F};
    float y{0.0F};
};

struct AssetVector3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct ModelVertex {
    AssetVector3 position{};
    AssetVector3 normal{};
    AssetVector2 texture_coordinate{};
};

struct ModelAsset {
    AssetIdentity identity;
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct WorldSurface {
    std::uint32_t first_index{0};
    std::uint32_t index_count{0};
    std::uint32_t material_index{0};
};

struct WorldAsset {
    AssetIdentity identity;
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WorldSurface> surfaces;
};

enum class ImagePixelFormat {
    rgba8,
};

struct ImageAsset {
    AssetIdentity identity;
    std::uint32_t width{0};
    std::uint32_t height{0};
    ImagePixelFormat pixel_format{ImagePixelFormat::rgba8};
    std::vector<std::byte> pixels;
};

struct SpriteFrame {
    ImageAsset image;
    float duration_seconds{0.0F};
};

struct SpriteAsset {
    AssetIdentity identity;
    std::vector<SpriteFrame> frames;
};

struct AudioAsset {
    AssetIdentity identity;
    std::uint32_t sample_rate{0};
    std::uint16_t channel_count{0};
    std::vector<float> interleaved_samples;
};

} // namespace hlclient::assets
