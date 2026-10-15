#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/client/temporary_effect.hpp>
#include <array>
#include <cstdint>
#include <optional>

namespace hlclient::game_api {
// A compiled module dependency, not a server-supplied filename or capability.
// The application opens only the selected approved world's exact root.
enum class DecalImageInterpretation : std::uint8_t {
    masked_palette_color,
    palette_tint_index_coverage,
};
struct DecalAssetRequest {
    std::array<char,32> wad_name{};
    std::array<char,16> texture_name{};
    DecalImageInterpretation interpretation{DecalImageInterpretation::masked_palette_color};
    LocalDecalMaterialMode material_mode{LocalDecalMaterialMode::lit_straight_alpha};
};
inline constexpr std::size_t kMaximumPreparedDecalAssets=16;
// Explicit committed placement: no local weapon, shot direction, camera,
// damage or lifetime clock is needed to materialize a persistent surface mark.
struct TemporarySurfaceDecalIntent {
    std::uint8_t asset_slot{};
    std::uint16_t server_index{};
    std::uint32_t target_entity{};
    std::optional<std::int16_t> target_model_reference;
    std::optional<client::CommittedDecalTargetPose> target_pose;
};
} // namespace hlclient::game_api
