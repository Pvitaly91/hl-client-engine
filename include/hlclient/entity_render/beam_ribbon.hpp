#pragma once
#include <hlclient/assets/asset_types.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace hlclient::entity_render {
inline constexpr unsigned beam_ribbon_max_segments=64U;
inline constexpr unsigned beam_ribbon_segment_count(bool sine) noexcept {
    return sine ? beam_ribbon_max_segments : 32U;
}
// Conservative transverse extent shared by geometry and visibility bounds.
// The sine profile's three-times radius and four turns are local-compatible
// presentation choices, not a claim about a private engine's stock constants.
inline float beam_ribbon_radius(float amplitude,bool sine) noexcept {
    if(!std::isfinite(amplitude) || amplitude<0)return 0;
    const auto radius=amplitude*(sine ? 3.F : 2.F);
    return std::isfinite(radius) ? radius : 0;
}
// Bounded renderer mechanism: absolute-time sampling, independent of frame rate.
// No game/resource identity, accumulated RNG or retained simulation.
inline std::array<assets::AssetVector3,beam_ribbon_max_segments+1U> beam_ribbon_points(
    assets::AssetVector3 a,assets::AssetVector3 b,float amplitude,bool sine,
    double seconds,std::uint32_t identity) noexcept {
    using V=assets::AssetVector3;
    const auto finite=[](V p){return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);};
    std::array<V,beam_ribbon_max_segments+1U> out{};
    const auto safe=finite(a) ? a : V{};out.fill(safe);
    const auto radius=beam_ribbon_radius(amplitude,sine);
    if(!finite(a) || !finite(b) || !std::isfinite(seconds) || !std::isfinite(amplitude) ||
        amplitude<0 || (amplitude>0 && radius==0))return out;
    V d{b.x-a.x,b.y-a.y,b.z-a.z};const auto length=std::hypot(d.x,d.y,d.z);
    if(!std::isfinite(length) || length<.001F)return out;
    // A very short wall trace must not compress a full-radius coil into a disc.
    const auto sine_radius=std::min(radius,length*.125F);
    d={d.x/length,d.y/length,d.z/length};V u=std::abs(d.z)<.9F ? V{-d.y,d.x,0} : V{0,-d.z,d.y};
    const auto un=std::hypot(u.x,u.y,u.z);u={u.x/un,u.y/un,u.z/un};
    const V v{d.y*u.z-d.z*u.y,d.z*u.x-d.x*u.z,d.x*u.y-d.y*u.x};
    const double clock=std::fmod(seconds,4096.);const double seed=(identity%997)*.173;
    for(unsigned i=0;i<out.size();++i){const auto t=static_cast<float>(i)/beam_ribbon_max_segments;
        const auto envelope=std::sin(t*3.14159265358979323846F);
        float x{},y{};
        if(sine) {
            const auto phase=clock*9+seed+static_cast<double>(t)*8*3.14159265358979323846;
            x=sine_radius*envelope*static_cast<float>(std::sin(phase));
            y=sine_radius*envelope*static_cast<float>(std::cos(phase));
        } else {
            // Sampling every second point retains all legacy 32-segment noise
            // coordinates. Additional sine tessellation does not alter it.
            const float phase=static_cast<float>(clock*9+seed+t*32*1.71);
            x=amplitude*envelope*std::sin(phase);y=amplitude*envelope*std::sin(phase*1.37F+1);
        }
        out[i]={a.x+(b.x-a.x)*t+u.x*x+v.x*y,a.y+(b.y-a.y)*t+u.y*x+v.y*y,a.z+(b.z-a.z)*t+u.z*x+v.z*y};
        if(!finite(out[i])) {out.fill(safe);return out;}
    }
    out.front()=a;out.back()=b;return out;
}
}
