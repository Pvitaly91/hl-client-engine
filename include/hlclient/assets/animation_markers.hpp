#pragma once
#include <hlclient/assets/model_asset_types.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
namespace hlclient::assets {
struct AnimationMarkerOccurrence { std::size_t ordinal{}; std::uint64_t loop{}; double seconds{}; };
struct AnimationMarkerCrossings {
    std::array<AnimationMarkerOccurrence,32> values{};
    std::size_t count{};
    std::uint64_t late{},overflow{};
};
// (previous,current], with previous=-1 admitting frame zero on a new restart.
// Times are sequence-relative. Rewind/repeated calls never emit. Bounded late
// catch-up considers at most eight recent loops; old occurrences are consumed.
inline AnimationMarkerCrossings animation_markers(std::span<const ModelSequenceEvent> events,
    double fps, std::uint32_t frames, bool looping, double previous, double current,
    std::optional<std::int32_t> event_filter = {}) noexcept {
    AnimationMarkerCrossings out;
    if(!std::isfinite(fps)||fps<=0||frames<2||!std::isfinite(previous)||
       !std::isfinite(current)||current<0||current<=previous) return out;
    const double duration=static_cast<double>(frames-1)/fps;
    const auto last=looping ? std::floor(current/duration) : 0.0;
    if(last>1e9) {out.overflow=1; return out;}
    const auto begin=looping ? std::max(0.0,std::floor(std::max(0.0,previous)/duration)) : 0.0;
    const auto first=std::max(begin,last-7.0);
    if(first>begin) for(const auto& e:events.first(std::min<std::size_t>(events.size(),256)))
        if((!event_filter || e.event_number==*event_filter) && e.frame>=0 && static_cast<std::uint32_t>(e.frame)<frames)
            out.late+=static_cast<std::uint64_t>(first-begin);
    for(auto loop=static_cast<std::uint64_t>(first); loop<=static_cast<std::uint64_t>(last); ++loop)
        for(std::size_t i=0;i<std::min<std::size_t>(events.size(),256);++i) {
            if(event_filter && events[i].event_number!=*event_filter) continue;
            const auto frame=events[i].frame;
            if(frame<0 || static_cast<std::uint32_t>(frame)>=frames) continue;
            const auto time=loop*duration+frame/fps;
            if(time<=previous || time>current) continue;
            if(current-time>0.25) {++out.late; continue;}
            if(out.count==out.values.size()) {++out.overflow; continue;}
            out.values[out.count++]={i,loop,time};
        }
    std::sort(out.values.begin(),out.values.begin()+out.count,[](const auto& a,const auto& b){
        if(a.seconds!=b.seconds) return a.seconds<b.seconds;
        if(a.loop!=b.loop) return a.loop<b.loop;
        return a.ordinal<b.ordinal;
    });
    return out;
}
}
