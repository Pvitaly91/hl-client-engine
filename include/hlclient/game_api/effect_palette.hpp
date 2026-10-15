#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace hlclient::game_api {
// Neutral owning RGB table from a verified 768-byte palette dependency.
// The selected module owns index interpretation; rendering only sees RGB.
enum class EffectPaletteStatus : std::uint8_t {
    not_requested, ready, absent, not_authorized, open_failed, decode_failed
};
struct EffectPaletteMetadata {
    EffectPaletteStatus status{EffectPaletteStatus::not_requested};
    std::array<std::array<std::uint8_t,3>,256> colors{};
};
// Startup evidence from the owned server's setting query. Unknown is distinct
// from disabled. A later real effect message retains its own authority.
struct EffectPresentationPolicy {
    std::optional<bool> server_blood_sprites_enabled;
};
} // namespace hlclient::game_api
