#pragma once

#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/game_api/surface_decals.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/hash/sha256.hpp>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::goldsrc {
enum class DecalAssetStatus : std::uint8_t {
    ready, pending, absent, not_authorized, open_failed, decode_failed,
    material_rejected, capacity,
};
[[nodiscard]] std::string_view to_string(DecalAssetStatus) noexcept;
struct DecalMipAudit {
    std::uint32_t width{},height{},index_zero_count{},index_255_count{},unique_indices{};
    std::uint8_t minimum_index{},maximum_index{};
};
// Owning bounded source evidence. No locator/native path/handle survives.
struct DecalAssetAudit {
    std::array<char,16> resolved_source_name{};
    std::uint8_t source_lump_type{};
    bool exact_spelling_match{};
    std::array<std::array<std::uint8_t,3>,256> palette{};
    std::array<DecalMipAudit,4> mips{};
    std::optional<hash::Sha256Digest> source_lump_digest;
    std::size_t source_lump_bytes{};
};
struct PreparedDecalAsset {
    DecalAssetStatus status{DecalAssetStatus::pending};
    std::shared_ptr<const assets::WorldTextureAsset> texture;
    DecalAssetAudit audit;
};
struct PreparedDecalAssets {
    std::array<PreparedDecalAsset,game_api::kMaximumPreparedDecalAssets> entries;
    std::size_t count{},source_bytes{},retained_decoded_bytes{};
    DecalAssetStatus archive_status{DecalAssetStatus::pending};
    std::optional<hash::Sha256Digest> source_digest;
};
// Startup-only bounded preparation. One compiled leaf WAD dependency, opened
// once through the exact selected-world root and verified source mechanism.
// Returns an owning immutable cache; there is no callback/frame file I/O.
[[nodiscard]] PreparedDecalAssets prepare_decal_assets(
    const PreparedSoundResources&,
    std::span<const game_api::DecalAssetRequest>) noexcept;
} // namespace hlclient::goldsrc
