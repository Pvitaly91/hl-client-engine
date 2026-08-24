#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::size_t kDefaultMaximumWorldTextureMaterials = 8'192U;
inline constexpr std::size_t kDefaultMaximumWorldTextureAssets = 512U;
inline constexpr std::size_t kDefaultMaximumWorldTextureWadReferences = 128U;
inline constexpr std::uint64_t kDefaultMaximumWorldTextureWadSourceBytes =
    64U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaximumWorldTextureWadLumps = 4'096U;
inline constexpr std::uint32_t kDefaultMaximumWorldTextureDimension = 4'096U;
inline constexpr std::uint64_t kDefaultMaximumWorldTextureTexels =
    16U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaximumDecodedBytesPerWorldTexture =
    64U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaximumTotalWorldTextureRgbaBytes =
    256U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaximumWorldspawnPairs = 256U;
inline constexpr std::size_t kDefaultMaximumWorldspawnValueBytes = 128U * 1024U;
inline constexpr std::size_t kDefaultMaximumWorldTextureMaterialsPerUpdate = 1U;
inline constexpr std::size_t
    kDefaultMaximumWorldTexturePixelConversionBytesPerUpdate = 64U * 1024U;
inline constexpr std::chrono::milliseconds kHardMaximumWorldTextureTimeout{
    60'000};
inline constexpr std::size_t kWorldTextureImportDiagnosticTextLimit = 256U;

using WorldTextureImportTimePoint = std::chrono::steady_clock::time_point;

struct GoldSrcWorldTextureImportLimits {
    std::size_t maximum_material_count{kDefaultMaximumWorldTextureMaterials};
    std::size_t maximum_texture_asset_count{kDefaultMaximumWorldTextureAssets};
    std::size_t maximum_wad_reference_count{
        kDefaultMaximumWorldTextureWadReferences};
    std::uint64_t maximum_wad_source_bytes{
        kDefaultMaximumWorldTextureWadSourceBytes};
    std::size_t maximum_wad_lump_count{
        kDefaultMaximumWorldTextureWadLumps};
    std::uint32_t maximum_texture_dimension{
        kDefaultMaximumWorldTextureDimension};
    std::uint64_t maximum_texture_texels{
        kDefaultMaximumWorldTextureTexels};
    std::size_t maximum_decoded_bytes_per_texture{
        kDefaultMaximumDecodedBytesPerWorldTexture};
    std::size_t maximum_total_decoded_rgba_bytes{
        kDefaultMaximumTotalWorldTextureRgbaBytes};
    std::size_t maximum_worldspawn_pairs{kDefaultMaximumWorldspawnPairs};
    std::size_t maximum_worldspawn_value_bytes{
        kDefaultMaximumWorldspawnValueBytes};
    std::size_t maximum_materials_per_update{
        kDefaultMaximumWorldTextureMaterialsPerUpdate};
    std::size_t maximum_pixel_conversion_bytes_per_update{
        kDefaultMaximumWorldTexturePixelConversionBytesPerUpdate};
    local_assets::LocalAssetSourceOpenLimits wad_source_open{
        kDefaultMaximumWorldTextureWadSourceBytes,
        local_assets::kDefaultLocalAssetSourceReadChunkBytes,
        1U,
        1U,
        std::nullopt};
    std::optional<std::chrono::milliseconds> timeout;
};

[[nodiscard]] bool valid_goldsrc_world_texture_import_limits(
    const GoldSrcWorldTextureImportLimits& limits) noexcept;

enum class WorldTextureImportState {
    idle,
    parsing_bsp_texture_sources,
    decoding_embedded_textures,
    parsing_worldspawn,
    resolving_wad_references,
    opening_wad,
    parsing_wad_catalog,
    resolving_external_textures,
    building_texture_set,
    textures_ready,
    textures_incomplete,
    cancelled,
    timed_out,
    failed,
};

enum class WorldTextureImportErrorCode {
    invalid_configuration,
    invalid_world_asset,
    bsp_source_missing,
    bsp_texture_source_parse_failed,
    embedded_texture_decode_failed,
    worldspawn_parse_failed,
    wad_reference_invalid,
    wad_source_resolution_failed,
    wad_source_open_failed,
    wad_catalog_failed,
    wad_texture_decode_failed,
    texture_set_build_failed,
    time_moved_backwards,
    cancelled,
    timed_out,
    unable_to_retain_state,
};

struct WorldTextureImportError {
    WorldTextureImportErrorCode code{
        WorldTextureImportErrorCode::invalid_configuration};
    std::optional<std::size_t> material_index;
    std::optional<std::uint32_t> source_texture_index;
    std::optional<std::uint32_t> archive_ordinal;
    std::optional<local_resources::LocalResourceResolutionCode>
        resolution_code;
    std::optional<local_assets::LocalAssetSourceOpenErrorCode>
        source_open_code;
    std::optional<assets::WorldTextureSetErrorCode> texture_set_code;
    std::string context;
};

struct WorldTextureImportProgress {
    std::size_t materials_considered{0U};
    std::size_t embedded_textures_decoded{0U};
    std::size_t wad_declarations_considered{0U};
    std::size_t wad_source_open_attempts{0U};
    std::size_t wad_sources_open{0U};
    std::size_t external_textures_decoded{0U};
    std::size_t pixel_conversion_bytes{0U};
};

class WorldTextureImportOperation;
struct WorldTextureImportBeginResult;

// Caller-driven local operation. The BSP byte span must remain valid until the
// operation reaches a terminal state. Production composition satisfies this
// by retaining ApprovedAssetDispatchState; the BSP is never reopened.
class WorldTextureImportOperation final {
public:
    [[nodiscard]] static WorldTextureImportBeginResult begin(
        const assets::WorldAsset& world,
        std::span<const std::byte> retained_bsp_source,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        GoldSrcWorldTextureImportLimits limits = {});

    ~WorldTextureImportOperation();
    WorldTextureImportOperation(WorldTextureImportOperation&&) noexcept;
    WorldTextureImportOperation& operator=(WorldTextureImportOperation&&)
        noexcept;
    WorldTextureImportOperation(const WorldTextureImportOperation&) = delete;
    WorldTextureImportOperation& operator=(const WorldTextureImportOperation&) =
        delete;

    void update(WorldTextureImportTimePoint now) noexcept;
    void cancel() noexcept;

    [[nodiscard]] WorldTextureImportState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const WorldTextureImportProgress& progress() const noexcept;
    [[nodiscard]] const assets::WorldTextureSet* result() const noexcept;
    [[nodiscard]] const WorldTextureImportError* error() const noexcept;
    [[nodiscard]] std::optional<assets::WorldTextureSet> take_result() noexcept;

private:
    class Implementation;
    explicit WorldTextureImportOperation(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

struct WorldTextureImportBeginResult {
    std::optional<WorldTextureImportOperation> operation;
    std::optional<WorldTextureImportError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return operation.has_value();
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    const WorldTextureImportState state) noexcept
{
    switch (state) {
    case WorldTextureImportState::idle: return "idle";
    case WorldTextureImportState::parsing_bsp_texture_sources:
        return "parsing_bsp_texture_sources";
    case WorldTextureImportState::decoding_embedded_textures:
        return "decoding_embedded_textures";
    case WorldTextureImportState::parsing_worldspawn:
        return "parsing_worldspawn";
    case WorldTextureImportState::resolving_wad_references:
        return "resolving_wad_references";
    case WorldTextureImportState::opening_wad: return "opening_wad";
    case WorldTextureImportState::parsing_wad_catalog:
        return "parsing_wad_catalog";
    case WorldTextureImportState::resolving_external_textures:
        return "resolving_external_textures";
    case WorldTextureImportState::building_texture_set:
        return "building_texture_set";
    case WorldTextureImportState::textures_ready: return "textures_ready";
    case WorldTextureImportState::textures_incomplete:
        return "textures_incomplete";
    case WorldTextureImportState::cancelled: return "cancelled";
    case WorldTextureImportState::timed_out: return "timed_out";
    case WorldTextureImportState::failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const WorldTextureImportErrorCode code) noexcept
{
    switch (code) {
    case WorldTextureImportErrorCode::invalid_configuration:
        return "invalid_configuration";
    case WorldTextureImportErrorCode::invalid_world_asset:
        return "invalid_world_asset";
    case WorldTextureImportErrorCode::bsp_source_missing:
        return "bsp_source_missing";
    case WorldTextureImportErrorCode::bsp_texture_source_parse_failed:
        return "bsp_texture_source_parse_failed";
    case WorldTextureImportErrorCode::embedded_texture_decode_failed:
        return "embedded_texture_decode_failed";
    case WorldTextureImportErrorCode::worldspawn_parse_failed:
        return "worldspawn_parse_failed";
    case WorldTextureImportErrorCode::wad_reference_invalid:
        return "wad_reference_invalid";
    case WorldTextureImportErrorCode::wad_source_resolution_failed:
        return "wad_source_resolution_failed";
    case WorldTextureImportErrorCode::wad_source_open_failed:
        return "wad_source_open_failed";
    case WorldTextureImportErrorCode::wad_catalog_failed:
        return "wad_catalog_failed";
    case WorldTextureImportErrorCode::wad_texture_decode_failed:
        return "wad_texture_decode_failed";
    case WorldTextureImportErrorCode::texture_set_build_failed:
        return "texture_set_build_failed";
    case WorldTextureImportErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case WorldTextureImportErrorCode::cancelled: return "cancelled";
    case WorldTextureImportErrorCode::timed_out: return "timed_out";
    case WorldTextureImportErrorCode::unable_to_retain_state:
        return "unable_to_retain_state";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
