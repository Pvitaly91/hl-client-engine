#pragma once

#include <algorithm>
#include <cstdint>

namespace hlclient::games::halflife {
// TE_BLOODSPRITE's byte is an effect size, not a literal sprite multiplier or
// a drop count. The SDK establishes the received size; these independently
// authored size conversions are auxiliary-informed, NOT stock measurements.
// FWGS e9b63241616c390d4d1720ced80e88de2acf83a6, cl_tent.c R_BloodSprite,
// blob95c85f7c17cfb5f02f3088a544fab954ff2665e7: spray size/35..size/25,
// drops size/25..size/15, count size plus two independent integer1..8 draws.
// We use midpoint divisors and the perturbation mean9, with the existing
// neutral burst budget16. This does not copy its RNG/physics/renderer bodies.
struct BloodSpriteProfile final {
    float spray_scale{}, drop_scale{};
    std::uint8_t drop_count{};
};
[[nodiscard]] constexpr BloodSpriteProfile blood_sprite_profile(std::uint8_t size) noexcept {
    if(!size)return {};
    return {static_cast<float>(size)/30.F,static_cast<float>(size)/20.F,
        static_cast<std::uint8_t>(std::min(16U,static_cast<unsigned>(size)+9U))};
}
} // namespace hlclient::games::halflife
