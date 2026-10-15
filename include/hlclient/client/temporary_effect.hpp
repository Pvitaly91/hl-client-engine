#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace hlclient::client {

// Owning neutral server observations. These are presentation evidence, not
// projectile, damage, timer, resource-open or rendering commands. Wire flags
// are preserved even when the selected module cannot present a component.
enum class TemporaryEffectKind : std::uint8_t {
    explosion,
    smoke,
    sparks,
    bubbles,
    bubble_trail,
    beam_follow,
    blood_sprite,
    decal,
};

// Owning canonical pose at the decal record's atomic publication. This is not
// another wire field or an inferred hit; pumping a later packet must not alter
// the model-local placement derived from an earlier committed server point.
struct CommittedDecalTargetPose final {
    std::uint32_t model_slot{};
    assets::AssetVector3 origin{},angles_degrees{};
};

struct CommittedTemporaryEffect final {
    std::uint64_t generation{}, record{};
    std::size_t record_ordinal{};
    std::uint32_t message_bit_offset{}, entry_ordinal{};
    double received_at_seconds{};
    TemporaryEffectKind kind{TemporaryEffectKind::explosion};
    assets::AssetVector3 origin{};
    std::uint16_t sprite_index{};
    std::uint8_t scale_tenths{}, framerate{}, flags{};
    // TE_BUBBLES' owning bounds/height/speed/count. Inert for other kinds.
    assets::AssetVector3 minimum{}, maximum{};
    float height{}, speed{};
    std::uint8_t count{};
    std::uint16_t entity_attachment{};
    std::uint8_t life_tenths{}, width_tenths{}, brightness{};
    std::array<std::uint8_t,3> color{};
    // Exact TE_BLOODSPRITE wire tokens. Signed model shorts and the full byte
    // range remain observations even when presentation cannot support them.
    std::array<std::int16_t,2> blood_sprite_indices{};
    std::uint8_t palette_color{}, blood_scale{};
    // Server decal observation, not SDK enum/WAD directory position. Byte
    // formats retain the reconstructed HIGH index; BSPDECAL retains its signed
    // short, including unsupported negative values for downstream validation.
    std::int32_t decal_index{};
    std::int16_t decal_entity{};
    std::uint8_t decal_subtype{};
    // Only TE_BSPDECAL on a non-world target carries a model reference.
    std::optional<std::int16_t> decal_model_reference;
    std::optional<CommittedDecalTargetPose> decal_target_pose;
};

} // namespace hlclient::client
