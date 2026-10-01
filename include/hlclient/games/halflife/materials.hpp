#pragma once
#include <hlclient/game_api/audio.hpp>
#include <hlclient/game_api/local_visuals.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::games::halflife {

enum class MaterialKind : std::uint8_t {
    concrete, metal, dirt, vent, grate, tile, slosh, wood, computer,
    glass, flesh, snow
};
enum class MaterialTableStatus : std::uint8_t {
    ready, missing, malformed, limit_exceeded
};
enum class MaterialSource : std::uint8_t {
    table_match, unknown_concrete_fallback, missing_table_concrete_fallback,
    malformed_table_concrete_fallback
};
struct MaterialLookup final {
    MaterialKind kind{MaterialKind::concrete};
    MaterialSource source{MaterialSource::missing_table_concrete_fallback};
    std::array<char,13> key{};
};

// Session-owned Half-Life table. No entry points into the borrowed source bytes.
class HalfLifeMaterials final {
public:
    void reset() noexcept;
    [[nodiscard]] MaterialTableStatus configure(std::string_view text) noexcept;
    [[nodiscard]] MaterialLookup lookup(std::string_view source_texture) const noexcept;
    [[nodiscard]] MaterialTableStatus status() const noexcept { return status_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] static std::optional<std::array<char,13>> normalize(
        std::string_view source_texture) noexcept;
private:
    struct Entry { std::array<char,13> key{}; MaterialKind kind{}; };
    std::array<Entry,512> entries_{};
    std::size_t count_{};
    MaterialTableStatus status_{MaterialTableStatus::missing};
};

[[nodiscard]] char material_code(MaterialKind) noexcept;
[[nodiscard]] std::string_view material_name(MaterialKind) noexcept;
[[nodiscard]] std::string_view material_source_name(MaterialSource) noexcept;
struct MaterialImpactSound final {
    game_api::LocalSoundReference reference;
    float volume{0.9F};
};
// One deterministic member of the pinned SDK material family. Flesh on a
// static BSP face is deliberately not an entity/blood sound.
[[nodiscard]] MaterialImpactSound material_impact_sound(MaterialKind,
    const game_api::LocalWeaponActionIdentity&, bool crowbar) noexcept;
[[nodiscard]] float crowbar_strike_volume(MaterialKind) noexcept;
[[nodiscard]] std::array<game_api::LocalSoundReference,24> material_impact_preload() noexcept;
// Separate optional sound in the pinned Glock BSP decal path. The SDK uses
// ric1..5 for roughly half of hits; the local gate is stable across replay.
[[nodiscard]] std::optional<game_api::LocalSoundReference> glock_ricochet_sound(
    const game_api::LocalWeaponActionIdentity&) noexcept;
[[nodiscard]] std::array<game_api::LocalSoundReference,5> glock_ricochet_preload() noexcept;
} // namespace hlclient::games::halflife
