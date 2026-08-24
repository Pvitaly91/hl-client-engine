#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

struct WorldVertex {
    AssetVector3 position{};
    AssetVector3 normal{};
    // Raw source texture-space coordinates. The meaning is named explicitly
    // by WorldAsset::texture_coordinate_space.
    AssetVector2 texture_coordinate{};
};

struct WorldBounds {
    AssetVector3 minimum{};
    AssetVector3 maximum{};
};

enum class WorldCoordinateSpace {
    source_native_goldsrc_z_up,
};

enum class WorldTextureCoordinateSpace {
    texel_units,
};

enum class WorldTextureStorage {
    missing,
    external_reference,
    embedded,
};

enum class WorldMaterialCompatibilityProfile {
    source_texture_reference_v1,
};

enum class WorldMaterialEvidenceProfile {
    validated_source_metadata,
};

struct WorldMaterialReference {
    std::optional<std::string> texture_name;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    WorldTextureStorage texture_storage{WorldTextureStorage::missing};
    std::int32_t source_texture_flags{0};
    std::optional<std::uint32_t> source_texinfo_index;
    WorldMaterialCompatibilityProfile compatibility_profile{
        WorldMaterialCompatibilityProfile::source_texture_reference_v1};
    WorldMaterialEvidenceProfile evidence_profile{
        WorldMaterialEvidenceProfile::validated_source_metadata};
};

struct WorldSurface {
    std::uint32_t first_index{0};
    std::uint32_t index_count{0};
    std::uint32_t material_index{0};
    WorldBounds bounds{};
    std::optional<std::uint32_t> source_surface_ordinal;
    std::optional<std::uint32_t> lightmap_offset;
    std::array<std::uint8_t, 4U> light_styles{};
    bool special_surface{false};
};

enum class WorldGeometrySourceProfile {
    unspecified,
    goldsrc_bsp_v30,
};

struct WorldGeometryStatistics {
    std::int32_t source_version{0};
    std::uint64_t source_model_count{0U};
    std::uint64_t source_face_count{0U};
    std::uint64_t world_model_source_face_count{0U};
    std::uint64_t skipped_submodel_face_count{0U};
    std::uint64_t emitted_surface_count{0U};
    std::uint64_t emitted_vertex_count{0U};
    std::uint64_t emitted_triangle_count{0U};
    std::uint64_t material_count{0U};
    std::uint64_t missing_texture_reference_count{0U};
    std::uint64_t external_texture_reference_count{0U};
    std::uint64_t embedded_texture_reference_count{0U};
};

struct WorldAsset {
    AssetIdentity identity;
    WorldCoordinateSpace coordinate_space{
        WorldCoordinateSpace::source_native_goldsrc_z_up};
    WorldTextureCoordinateSpace texture_coordinate_space{
        WorldTextureCoordinateSpace::texel_units};
    WorldBounds bounds{};
    std::optional<WorldBounds> source_model_bounds;
    std::vector<WorldVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WorldSurface> surfaces;
    std::vector<WorldMaterialReference> materials;
    WorldGeometrySourceProfile source_profile{
        WorldGeometrySourceProfile::unspecified};
    WorldGeometryStatistics statistics{};
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
