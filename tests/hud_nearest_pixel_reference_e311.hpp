#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hlclient::tests::hud_nearest_reference {

// GL_NEAREST samples interpolated normalized UV, not the CPU's ideal local
// coordinate. At an exact texel boundary only, finite interpolation precision
// may select either adjacent texel. Never allow a pixel outside the crop or a
// wider RGB tolerance; callers compare one complete source-derived RGB tuple.
inline std::array<std::uint32_t,2> candidates(float position,std::uint32_t size) {
    const auto index=static_cast<std::uint32_t>(std::clamp(std::floor(position),0.0F,
        static_cast<float>(size-1U)));
    const float edge=std::round(position);
    if(edge>=1.0F && edge<static_cast<float>(size) && std::abs(position-edge)<=.0001F) {
        const auto upper=static_cast<std::uint32_t>(edge);
        return {upper-1U,upper};
    }
    return {index,index};
}

inline bool close(std::array<unsigned,3> actual,std::array<unsigned,3> expected) {
    for(unsigned c=0;c<3;++c)
        if(std::abs(static_cast<int>(actual[c])-static_cast<int>(expected[c]))>3)return false;
    return true;
}

} // namespace hlclient::tests::hud_nearest_reference
